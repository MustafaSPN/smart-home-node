import SwiftUI
import Combine
import FirebaseDatabase
import FirebaseAuth

// ============================================================
// DATA MODELS
// ============================================================
struct Outage: Identifiable {
    var id: String
    var start: String
    var end: String
    var durationMin: Int
}

// ============================================================
// FIREBASE MANAGER — per-user paths
// ============================================================
class FirebaseManager: ObservableObject {
    @Published var isPowerAvailable: Bool = false
    @Published var isPcTurningOn: Bool = false
    @Published var outageHistory: [Outage] = []
    @Published var lastPingTime: String = "—"

    // Air conditioner (mirrors users/{uid}/climate/state)
    @Published var acPower: Bool = false
    @Published var acTemp: Int = 24
    @Published var acMode: String = "cool"   // cool | heat | dry | fan | auto
    @Published var acFan: String = "auto"    // auto | low | med | high
    @Published var roomTemp: Double? = nil
    @Published var roomHum: Double? = nil

    private var ref: DatabaseReference?
    private var handles: [DatabaseHandle] = []

    init() {}

    func startConnection() {
        guard let uid = Auth.auth().currentUser?.uid else { return }
        if ref == nil {
            ref = Database.database().reference()
        }
        startListening(uid: uid)
    }

    func stopListening() {
        guard let ref = ref, let uid = Auth.auth().currentUser?.uid else { return }
        let base = ref.child("users/\(uid)")
        for handle in handles { base.removeObserver(withHandle: handle) }
        handles.removeAll()
    }

    private func startListening(uid: String) {
        guard let ref = ref else { return }
        let base = ref.child("users/\(uid)")

        let h1 = base.child("status/power_available").observe(.value) { snapshot in
            if let value = snapshot.value as? Bool {
                DispatchQueue.main.async {
                    withAnimation(.easeInOut(duration: 0.5)) { self.isPowerAvailable = value }
                }
            }
        }
        handles.append(h1)

        let h2 = base.child("status/last_ping").observe(.value) { snapshot in
            if let timestamp = snapshot.value as? Double {
                let date = Date(timeIntervalSince1970: timestamp / 1000)
                let formatter = DateFormatter()
                formatter.dateFormat = "dd.MM.yyyy HH:mm"
                formatter.timeZone = TimeZone(identifier: "Europe/Istanbul")
                DispatchQueue.main.async { self.lastPingTime = formatter.string(from: date) }
            }
        }
        handles.append(h2)

        let h3 = base.child("command/pc_on").observe(.value) { snapshot in
            if let value = snapshot.value as? Bool {
                DispatchQueue.main.async {
                    withAnimation(.easeInOut(duration: 0.3)) { self.isPcTurningOn = value }
                }
            }
        }
        handles.append(h3)

        let h4 = base.child("history").observe(.value) { snapshot in
            var newHistory: [Outage] = []
            if let children = snapshot.children.allObjects as? [DataSnapshot] {
                for child in children {
                    if let data = child.value as? [String: Any],
                       let start = data["start"] as? String {
                        let end = data["end"] as? String ?? "Ongoing..."
                        let duration = data["duration_min"] as? Int ?? 0
                        newHistory.insert(Outage(id: child.key, start: start, end: end, durationMin: duration), at: 0)
                    }
                }
            }
            DispatchQueue.main.async {
                withAnimation(.easeInOut(duration: 0.3)) { self.outageHistory = newHistory }
            }
        }
        handles.append(h4)

        let h5 = base.child("climate/state").observe(.value) { snapshot in
            guard let dict = snapshot.value as? [String: Any] else { return }
            DispatchQueue.main.async {
                if let p = dict["power"] as? Bool { self.acPower = p }
                if let t = (dict["temp"] as? NSNumber)?.intValue { self.acTemp = t }
                if let m = dict["mode"] as? String { self.acMode = m }
                if let f = dict["fan"] as? String { self.acFan = f }
            }
        }
        handles.append(h5)

        let h6 = base.child("status/room_temp").observe(.value) { snapshot in
            let v = (snapshot.value as? NSNumber)?.doubleValue
            DispatchQueue.main.async { self.roomTemp = v }
        }
        handles.append(h6)

        let h7 = base.child("status/room_hum").observe(.value) { snapshot in
            let v = (snapshot.value as? NSNumber)?.doubleValue
            DispatchQueue.main.async { self.roomHum = v }
        }
        handles.append(h7)
    }

    func sendPcWakeCommand() {
        guard let uid = Auth.auth().currentUser?.uid, let ref = ref else { return }
        withAnimation(.easeInOut(duration: 0.3)) { isPcTurningOn = true }
        let commandRef = ref.child("users/\(uid)/command/pc_on")
        commandRef.setValue(false) { _, _ in
            commandRef.setValue(true) { error, _ in
                if let error = error {
                    debugPrint("❌ PC wake command error: \(error.localizedDescription)")
                    DispatchQueue.main.async { withAnimation { self.isPcTurningOn = false } }
                }
            }
        }
        scheduleWakeTimeout(uid: uid, ref: ref)
    }

    // Write the desired AC state; the ESP32-S3 mirrors it to IR.
    func setAcState(power: Bool? = nil, temp: Int? = nil,
                    mode: String? = nil, fan: String? = nil) {
        guard let uid = Auth.auth().currentUser?.uid, let ref = ref else { return }
        if let power = power { acPower = power }
        if let temp = temp { acTemp = min(30, max(16, temp)) }
        if let mode = mode { acMode = mode; acPower = true }
        if let fan = fan { acFan = fan }

        let payload: [String: Any] = [
            "power": acPower,
            "temp": acTemp,
            "mode": acMode,
            "fan": acFan,
            "updated": ServerValue.timestamp()
        ]
        ref.child("users/\(uid)/climate/state").setValue(payload)
    }

    // Safety net: if the ESP never resets command/pc_on (offline, error, etc.)
    // the button would stay disabled forever. After 25s, force-clear the flag
    // so the user can retry.
    private func scheduleWakeTimeout(uid: String, ref: DatabaseReference) {
        DispatchQueue.main.asyncAfter(deadline: .now() + 25) { [weak self] in
            guard let self = self, self.isPcTurningOn else { return }
            debugPrint("⏱️ PC wake timed out — clearing command flag")
            ref.child("users/\(uid)/command/pc_on").setValue(false)
            withAnimation { self.isPcTurningOn = false }
        }
    }
}

// ============================================================
// MAIN VIEW
// ============================================================
struct MainView: View {
    @EnvironmentObject var authManager: AuthStateManager
    @StateObject var fbManager = FirebaseManager()
    @Namespace private var glassNamespace
    @State private var isAnimationActive = false
    @State private var showProfile = false
    @State private var showClimate = false

    var body: some View {
        NavigationView {
            ZStack {
                Color.black.ignoresSafeArea()
                ZStack {
                    Circle()
                        .fill(RadialGradient(colors: [Color.purple.opacity(0.6), .clear], center: .center, startRadius: 20, endRadius: 160))
                        .frame(width: 320, height: 320)
                        .offset(x: isAnimationActive ? -30 : 50, y: isAnimationActive ? -80 : -180)
                    Circle()
                        .fill(RadialGradient(colors: [Color.cyan.opacity(0.5), Color.blue.opacity(0.0)], center: .center, startRadius: 10, endRadius: 140))
                        .frame(width: 280, height: 280)
                        .offset(x: isAnimationActive ? 80 : -60, y: isAnimationActive ? 100 : 200)
                    Circle()
                        .fill(RadialGradient(colors: [(fbManager.isPowerAvailable ? Color.green : Color.red).opacity(0.4), .clear], center: .center, startRadius: 10, endRadius: 120))
                        .frame(width: 240, height: 240)
                        .offset(x: isAnimationActive ? -70 : 40, y: isAnimationActive ? 300 : 50)
                    Circle()
                        .fill(RadialGradient(colors: [Color.indigo.opacity(0.4), .clear], center: .center, startRadius: 20, endRadius: 180))
                        .frame(width: 360, height: 360)
                        .offset(x: isAnimationActive ? 60 : -80, y: isAnimationActive ? -200 : 100)
                }
                .ignoresSafeArea()
                .animation(.easeInOut(duration: 8).repeatForever(autoreverses: true), value: isAnimationActive)

                GlassEffectContainer {
                    VStack(spacing: 35) {
                        statusCard
                        turnOnPcButton
                        climateCard
                        outageHistoryView
                    }
                    .padding()
                }
            }
            .navigationTitle("HomeNode")
            .navigationBarTitleDisplayMode(.inline)
            .toolbarColorScheme(.dark, for: .navigationBar)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button(action: { showProfile = true }) {
                        ZStack {
                            Circle()
                                .fill(LinearGradient(colors: [.purple, .cyan], startPoint: .topLeading, endPoint: .bottomTrailing))
                                .frame(width: 34, height: 34)
                            Text(emailInitial)
                                .font(.system(size: 14, weight: .bold))
                                .foregroundStyle(.white)
                        }
                    }
                    .accessibilityLabel("Profile")
                }
            }
        }
        .sheet(isPresented: $showProfile) {
            ProfileView().environmentObject(authManager)
        }
        .sheet(isPresented: $showClimate) {
            ClimateControlView(fb: fbManager)
        }
        .onAppear {
            fbManager.startConnection()
            isAnimationActive = true
        }
        .onDisappear { fbManager.stopListening() }
        .preferredColorScheme(.dark)
    }

    private var emailInitial: String {
        String(Auth.auth().currentUser?.email?.prefix(1).uppercased() ?? "?")
    }

    private var statusCard: some View {
        VStack(spacing: 12) {
            HStack {
                Circle()
                    .fill(fbManager.isPowerAvailable ? Color.green : Color.red)
                    .frame(width: 14, height: 14)
                    .shadow(color: fbManager.isPowerAvailable ? .green.opacity(0.8) : .red.opacity(0.8), radius: 8)
                Text(fbManager.isPowerAvailable ? "System Online" : "System Offline")
                    .font(.headline).fontWeight(.semibold)
                Spacer()
                Image(systemName: fbManager.isPowerAvailable ? "bolt.fill" : "bolt.slash.fill")
                    .font(.title2)
                    .foregroundStyle(fbManager.isPowerAvailable ? .green : .red)
                    .contentTransition(.symbolEffect(.replace))
            }
            HStack {
                Image(systemName: "antenna.radiowaves.left.and.right").font(.caption).foregroundStyle(.secondary)
                Text("Last Ping: \(fbManager.lastPingTime)").font(.caption).foregroundStyle(.secondary)
                Spacer()
            }
        }
        .padding()
        .frame(maxWidth: .infinity)
        .glassEffect(.regular, in: .rect(cornerRadius: 24))
        .glassEffectID("status", in: glassNamespace)
    }

    private var turnOnPcButton: some View {
        Button(action: { fbManager.sendPcWakeCommand() }) {
            HStack(spacing: 15) {
                Image(systemName: "power")
                    .font(.system(size: 28, weight: .bold))
                    .foregroundStyle(fbManager.isPcTurningOn ? .orange : (fbManager.isPowerAvailable ? .primary : .secondary))
                    .contentTransition(.symbolEffect(.replace))
                VStack(alignment: .leading, spacing: 2) {
                    Text(fbManager.isPcTurningOn ? "Turning On PC..." : "Turn On PC")
                        .font(.title3).fontWeight(.bold)
                    Text(fbManager.isPcTurningOn ? "Sending Wake on LAN" : "Remote start via Wake on LAN")
                        .font(.caption).foregroundStyle(.secondary)
                }
                Spacer()
                if fbManager.isPcTurningOn {
                    ProgressView().tint(.orange)
                } else {
                    Image(systemName: "chevron.right").foregroundStyle(.tertiary)
                }
            }
            .padding()
            .frame(maxWidth: .infinity, minHeight: 80)
        }
        .glassEffect(.regular.interactive(), in: .rect(cornerRadius: 24))
        .glassEffectID("pcac", in: glassNamespace)
        .buttonStyle(PcAcButtonStyle())
        .disabled(!fbManager.isPowerAvailable || fbManager.isPcTurningOn)
        .opacity((!fbManager.isPowerAvailable || fbManager.isPcTurningOn) ? 0.5 : 1.0)
    }

    private var climateCard: some View {
        Button(action: { showClimate = true }) {
            VStack(spacing: 12) {
                HStack {
                    Image(systemName: "snowflake")
                        .font(.title2)
                        .foregroundStyle(fbManager.acPower ? .cyan : .secondary)
                    Text("Climate").font(.headline).fontWeight(.semibold)
                    Spacer()
                    Text(fbManager.acPower ? "\(fbManager.acTemp)°C · \(acModeLabel(fbManager.acMode))" : "Off")
                        .font(.subheadline).fontWeight(.medium)
                        .foregroundStyle(fbManager.acPower ? .cyan : .secondary)
                    Image(systemName: "chevron.right").foregroundStyle(.tertiary)
                }
                HStack(spacing: 16) {
                    Label(fbManager.roomTemp.map { String(format: "%.1f°C", $0) } ?? "—",
                          systemImage: "thermometer.medium")
                    Label(fbManager.roomHum.map { String(format: "%.0f%%", $0) } ?? "—",
                          systemImage: "humidity")
                    Spacer()
                }
                .font(.caption).foregroundStyle(.secondary)
            }
            .padding()
            .frame(maxWidth: .infinity)
        }
        .buttonStyle(.plain)
        .glassEffect(.regular, in: .rect(cornerRadius: 24))
        .glassEffectID("climate", in: glassNamespace)
    }

    private var outageHistoryView: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Image(systemName: "clock.arrow.circlepath").foregroundStyle(.secondary)
                Text("Outage History").font(.title3).fontWeight(.bold)
                Spacer()
                Text("\(fbManager.outageHistory.count)")
                    .font(.caption).fontWeight(.semibold)
                    .padding(.horizontal, 10).padding(.vertical, 4)
                    .background(.quaternary, in: Capsule())
            }
            .padding()
            Divider().padding(.horizontal)
            if fbManager.outageHistory.isEmpty {
                VStack(spacing: 12) {
                    Image(systemName: "checkmark.seal.fill").font(.system(size: 40)).foregroundStyle(.green.opacity(0.6))
                    Text("No recorded power outages").font(.subheadline).foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, minHeight: 120).padding()
            } else {
                ScrollView(showsIndicators: false) {
                    LazyVStack(spacing: 10) {
                        ForEach(fbManager.outageHistory) { outage in outageRow(outage) }
                    }
                    .padding()
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: 280)
        .glassEffect(.regular, in: .rect(cornerRadius: 24))
        .glassEffectID("gecmis", in: glassNamespace)
    }

    private func outageRow(_ outage: Outage) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Image(systemName: "arrow.down.circle.fill").foregroundStyle(.red).font(.caption)
                Text("Start").font(.caption2).foregroundStyle(.secondary)
                Spacer()
                Text(outage.start).font(.caption).fontWeight(.medium)
            }
            HStack(spacing: 8) {
                Image(systemName: "arrow.up.circle.fill").foregroundStyle(.green).font(.caption)
                Text("End").font(.caption2).foregroundStyle(.secondary)
                Spacer()
                Text(outage.end.isEmpty ? "Ongoing..." : outage.end)
                    .font(.caption).fontWeight(.medium)
                    .foregroundStyle(outage.end.isEmpty ? .orange : .primary)
            }
            HStack(spacing: 8) {
                Image(systemName: "timer").foregroundStyle(.orange).font(.caption)
                Text("Duration").font(.caption2).foregroundStyle(.secondary)
                Spacer()
                Text(formatDuration(outage.durationMin))
                    .font(.caption).fontWeight(.bold)
                    .foregroundStyle(outage.durationMin > 60 ? .red : .orange)
            }
        }
        .padding(12)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 14))
    }

    private func formatDuration(_ minutes: Int) -> String {
        guard minutes > 0 else { return "—" }
        if minutes < 60 { return "\(minutes) Min" }
        let days = minutes / 1440
        let hours = (minutes % 1440) / 60
        let mins = minutes % 60
        if days > 0 {
            return "\(days)d \(hours)h \(mins)m"
        }
        return "\(hours)h \(mins)m"
    }
}

// ============================================================
// PC ON BUTTON STYLE
// ============================================================
struct PcAcButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? 0.96 : 1.0)
            .brightness(configuration.isPressed ? 0.08 : 0)
            .animation(.easeInOut(duration: 0.12), value: configuration.isPressed)
            .onChange(of: configuration.isPressed) { _, isPressed in
                if isPressed {
                    let impact = UIImpactFeedbackGenerator(style: .medium)
                    impact.impactOccurred()
                }
            }
    }
}
