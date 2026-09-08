import { onRequest, onCall, HttpsError } from "firebase-functions/v2/https";
import { onSchedule } from "firebase-functions/v2/scheduler";
import { initializeApp } from "firebase-admin/app";
import { getAuth } from "firebase-admin/auth";
import { getDatabase } from "firebase-admin/database";
import { getMessaging } from "firebase-admin/messaging";

// RTDB URL comes from the environment (see .env.example), so no project
// identifier is hardcoded in the repository.
initializeApp({
  databaseURL: process.env.RTDB_URL,
});

const db = getDatabase();

// ============================================================
// 1. ESP PING — ESP32 calls this every 5 minutes
//    Header: Authorization: Bearer <Firebase ID token>
//
//    The device signs in to Firebase Auth with its own account and
//    sends the resulting ID token. The UID is taken from the verified
//    token only — a client-supplied UID is never trusted.
// ============================================================
export const espPing = onRequest(
  {
    region: "europe-west1",
    invoker: "public",
  },
  async (req, res) => {
    const header = req.headers.authorization;
    if (!header || !header.startsWith("Bearer ")) {
      res.set("WWW-Authenticate", "Bearer");
      res.status(401).json({ error: "Missing bearer token" });
      return;
    }

    const idToken = header.slice("Bearer ".length).trim();
    if (!idToken) {
      res.set("WWW-Authenticate", "Bearer");
      res.status(401).json({ error: "Missing bearer token" });
      return;
    }

    // Verify signature, audience, issuer and expiry. checkRevoked also
    // rejects tokens for accounts that have been disabled or had their
    // sessions revoked, at the cost of one extra lookup per ping.
    let uid: string;
    try {
      const decoded = await getAuth().verifyIdToken(idToken, true);
      uid = decoded.uid;
    } catch (error) {
      // Log the error code only — never the token or its payload.
      const code =
        typeof error === "object" && error !== null && "code" in error
          ? String((error as { code: unknown }).code)
          : "auth/unknown-error";
      console.warn(`espPing rejected token: ${code}`);
      res.set("WWW-Authenticate", "Bearer");
      res.status(401).json({ error: "Invalid or expired token" });
      return;
    }

    const now = Date.now();
    const userRef = db.ref(`users/${uid}`);

    try {
      const statusSnapshot = await userRef.child("status/power_available").get();
      const previousStatus = statusSnapshot.val();

      await userRef.child("status").update({
        last_ping: now,
        power_available: true,
      });

      // Power restored — close open outage record
      if (previousStatus === false) {
        const historySnapshot = await userRef
          .child("history")
          .orderByChild("start_ts")
          .limitToLast(1)
          .get();

        if (historySnapshot.exists()) {
          const entries = historySnapshot.val();
          const key = Object.keys(entries)[0];
          const outage = entries[key];

          if (outage.end_ts === 0) {
            const durationMin = Math.round((now - outage.start_ts) / 60000);
            await userRef.child(`history/${key}`).update({
              end: formatTarih(now),
              end_ts: now,
              duration_min: durationMin,
            });
          }
        }

        await bildirimGonder(uid, "✅ Power Restored!", "Home power is back on.");
      }

      res.status(200).json({ success: true, timestamp: now });
    } catch (error) {
      console.error("espPing error:", error);
      res.status(500).json({ error: "Server error" });
    }
  }
);

// ============================================================
// 2. CHECK POWER — Runs every 2 minutes (Scheduler)
//    Iterates all users and checks each one's last_ping
// ============================================================
export const checkPower = onSchedule(
  {
    schedule: "every 2 minutes",
    region: "europe-west1",
    timeZone: "Europe/Istanbul",
  },
  async () => {
    try {
      const usersSnapshot = await db.ref("users").get();
      if (!usersSnapshot.exists()) return;

      const now = Date.now();
      const users = usersSnapshot.val() as Record<string, unknown>;

      for (const uid of Object.keys(users)) {
        const userRef = db.ref(`users/${uid}`);
        const statusSnap = await userRef.child("status").get();
        const status = statusSnap.val();

        if (!status || !status.last_ping) continue;

        const lastPing = status.last_ping as number;
        const diffMin = (now - lastPing) / 60000;

        if (diffMin > 10 && status.power_available === true) {
          console.log(`⚡ Power outage detected for user ${uid}`);

          await userRef.child("status/power_available").set(false);

          const startStr = formatTarih(lastPing);
          await userRef.child("history").push({
            start: startStr,
            start_ts: lastPing,
            end: "",
            end_ts: 0,
            duration_min: 0,
          });

          await bildirimGonder(
            uid,
            "⚡ Power Outage!",
            `Home power was cut off as of ${startStr}.`
          );
        }
      }
    } catch (error) {
      console.error("checkPower error:", error);
    }
  }
);

// ============================================================
// 3. TURN ON PC — Called from app (Callable)
//    Writes pc_on=true to the caller's user path.
//    ESP32 listens to this path and sends WoL.
// ============================================================
export const turnOnPc = onCall(
  { region: "europe-west1" },
  async (request) => {
    if (!request.auth) {
      throw new HttpsError(
        "unauthenticated",
        "You must be logged in for this operation."
      );
    }
    const uid = request.auth.uid;
    try {
      await db.ref(`users/${uid}/command/pc_on`).set(true);
      return { success: true, message: "PC turn on command sent!" };
    } catch (error) {
      console.error("turnOnPc error:", error);
      throw new HttpsError("internal", "Failed to send PC turn on command.");
    }
  }
);

// ============================================================
// HELPERS
// ============================================================

/**
 * Sends FCM notification to a specific user's registered token
 */
async function bildirimGonder(
  uid: string,
  baslik: string,
  mesaj: string
): Promise<void> {
  try {
    const tokenSnap = await db.ref(`users/${uid}/fcm_token`).get();
    if (!tokenSnap.exists()) return;

    const token = tokenSnap.val() as string;
    await getMessaging().send({
      notification: { title: baslik, body: mesaj },
      apns: { payload: { aps: { sound: "default", badge: 1 } } },
      token,
    });
  } catch (error) {
    console.error("bildirimGonder error:", error);
  }
}

function formatTarih(timestamp: number): string {
  const date = new Date(timestamp);
  const options: Intl.DateTimeFormatOptions = {
    timeZone: "Europe/Istanbul",
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
    hour: "2-digit",
    minute: "2-digit",
    hour12: false,
  };
  return date.toLocaleString("en-US", options);
}
