import SwiftUI
import FirebaseAuth
import FirebaseDatabase

// ============================================================
// PROFILE VIEW
// ============================================================
struct ProfileView: View {
    @EnvironmentObject var authManager: AuthStateManager
    @Environment(\.dismiss) var dismiss

    @State private var macAddress = ""
    @State private var savedMac = ""
    @State private var macSaveStatus: SaveStatus = .idle
    @State private var newPassword = ""
    @State private var confirmNewPassword = ""
    @State private var passwordChangeStatus: SaveStatus = .idle
    @State private var showDeleteConfirm = false
    @State private var deleteError = ""
    @State private var isLoadingMac = true

    enum SaveStatus { case idle, saving, success, error(String) }

    private var ref: DatabaseReference {
        Database.database().reference()
    }

    private var uid: String {
        Auth.auth().currentUser?.uid ?? ""
    }

    var body: some View {
        NavigationView {
            ZStack {
                Color.black.ignoresSafeArea()
                Circle()
                    .fill(RadialGradient(colors: [.purple.opacity(0.35), .clear], center: .center, startRadius: 10, endRadius: 200))
                    .frame(width: 400, height: 400)
                    .offset(x: -60, y: -200)
                    .ignoresSafeArea()

                ScrollView(showsIndicators: false) {
                    VStack(spacing: 24) {

                        // ── Avatar + Email ──────────────────────────
                        VStack(spacing: 10) {
                            ZStack {
                                Circle()
                                    .fill(LinearGradient(colors: [.purple, .cyan], startPoint: .topLeading, endPoint: .bottomTrailing))
                                    .frame(width: 72, height: 72)
                                Text(emailInitial)
                                    .font(.system(size: 28, weight: .bold))
                                    .foregroundStyle(.white)
                            }
                            Text(Auth.auth().currentUser?.email ?? "—")
                                .font(.headline)
                                .foregroundStyle(.white)
                            Text("HomeNode Account")
                                .font(.caption)
                                .foregroundStyle(.white.opacity(0.5))
                        }
                        .padding(.top, 8)

                        // ── MAC Address Card ────────────────────────
                        profileCard(title: "PC Mac Address", icon: "desktopcomputer") {
                            VStack(alignment: .leading, spacing: 12) {
                                Text("Enter your PC's MAC address for Wake on LAN.")
                                    .font(.caption)
                                    .foregroundStyle(.white.opacity(0.55))

                                MacAddressField(text: $macAddress)

                                if !savedMac.isEmpty {
                                    HStack(spacing: 6) {
                                        Image(systemName: "checkmark.circle.fill").foregroundStyle(.green).font(.caption)
                                        Text("Saved: \(savedMac)").font(.caption).foregroundStyle(.white.opacity(0.6))
                                    }
                                }

                                Button(action: saveMac) {
                                    macSaveLabel
                                        .frame(maxWidth: .infinity, minHeight: 44)
                                }
                                .background(
                                    LinearGradient(colors: [.purple, .cyan], startPoint: .leading, endPoint: .trailing),
                                    in: RoundedRectangle(cornerRadius: 12)
                                )
                                .foregroundStyle(.white)
                            }
                        }

                        // ── Change Password Card ────────────────────
                        profileCard(title: "Change Password", icon: "lock.rotation") {
                            VStack(spacing: 12) {
                                HNTextField(icon: "lock.fill", placeholder: "New Password", text: $newPassword, isSecure: true)
                                HNTextField(icon: "lock.fill", placeholder: "Confirm New Password", text: $confirmNewPassword, isSecure: true)

                                Button(action: changePassword) {
                                    passwordChangeLabel
                                        .frame(maxWidth: .infinity, minHeight: 44)
                                }
                                .background(
                                    LinearGradient(colors: [.indigo, .purple], startPoint: .leading, endPoint: .trailing),
                                    in: RoundedRectangle(cornerRadius: 12)
                                )
                                .foregroundStyle(.white)
                            }
                        }

                        // ── Account Actions Card ────────────────────
                        profileCard(title: "Account", icon: "person.crop.circle") {
                            VStack(spacing: 12) {
                                Button(action: signOut) {
                                    HStack {
                                        Image(systemName: "rectangle.portrait.and.arrow.right")
                                        Text("Sign Out")
                                            .fontWeight(.semibold)
                                    }
                                    .frame(maxWidth: .infinity, minHeight: 44)
                                }
                                .background(.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 12))
                                .overlay(RoundedRectangle(cornerRadius: 12).stroke(.white.opacity(0.15)))
                                .foregroundStyle(.white)

                                Button(action: { showDeleteConfirm = true }) {
                                    HStack {
                                        Image(systemName: "trash.fill")
                                        Text("Delete Account")
                                            .fontWeight(.semibold)
                                    }
                                    .frame(maxWidth: .infinity, minHeight: 44)
                                }
                                .background(.red.opacity(0.15), in: RoundedRectangle(cornerRadius: 12))
                                .overlay(RoundedRectangle(cornerRadius: 12).stroke(.red.opacity(0.3)))
                                .foregroundStyle(.red)

                                if !deleteError.isEmpty {
                                    Text(deleteError).font(.caption).foregroundStyle(.red.opacity(0.8))
                                }
                            }
                        }

                        Spacer().frame(height: 40)
                    }
                    .padding(.horizontal, 20)
                    .padding(.top, 16)
                }
            }
            .navigationTitle("Profile")
            .navigationBarTitleDisplayMode(.inline)
            .toolbarColorScheme(.dark, for: .navigationBar)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Done") { dismiss() }
                        .foregroundStyle(
                            LinearGradient(colors: [.purple, .cyan], startPoint: .leading, endPoint: .trailing)
                        )
                }
            }
            .alert("Delete Account", isPresented: $showDeleteConfirm) {
                Button("Cancel", role: .cancel) {}
                Button("Delete", role: .destructive) { deleteAccount() }
            } message: {
                Text("This action cannot be undone. Your account and all data will be permanently deleted.")
            }
        }
        .preferredColorScheme(.dark)
        .onAppear { loadMac() }
    }

    // ── Helpers ────────────────────────────────────────────────

    private var emailInitial: String {
        String(Auth.auth().currentUser?.email?.prefix(1).uppercased() ?? "?")
    }

    @ViewBuilder
    private var macSaveLabel: some View {
        switch macSaveStatus {
        case .idle:
            HStack { Image(systemName: "square.and.arrow.down.fill"); Text("Save MAC Address").fontWeight(.semibold) }
        case .saving:
            ProgressView().tint(.white)
        case .success:
            HStack { Image(systemName: "checkmark.circle.fill"); Text("Saved!").fontWeight(.semibold) }
        case .error(let msg):
            Text(msg).font(.caption)
        }
    }

    @ViewBuilder
    private var passwordChangeLabel: some View {
        switch passwordChangeStatus {
        case .idle:
            HStack { Image(systemName: "key.fill"); Text("Update Password").fontWeight(.semibold) }
        case .saving:
            ProgressView().tint(.white)
        case .success:
            HStack { Image(systemName: "checkmark.circle.fill"); Text("Password Updated!").fontWeight(.semibold) }
        case .error(let msg):
            Text(msg).font(.caption)
        }
    }

    @ViewBuilder
    private func profileCard<Content: View>(title: String, icon: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack(spacing: 8) {
                Image(systemName: icon)
                    .foregroundStyle(.purple)
                Text(title)
                    .font(.headline)
                    .foregroundStyle(.white)
            }
            content()
        }
        .padding(20)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.white.opacity(0.06), in: RoundedRectangle(cornerRadius: 20))
        .overlay(RoundedRectangle(cornerRadius: 20).stroke(.white.opacity(0.1)))
    }

    // ── Actions ────────────────────────────────────────────────

    private func loadMac() {
        ref.child("users/\(uid)/profile/mac_address").observeSingleEvent(of: .value) { snapshot in
            DispatchQueue.main.async {
                isLoadingMac = false
                if let mac = snapshot.value as? String {
                    savedMac = mac
                    macAddress = mac
                }
            }
        }
    }

    private func saveMac() {
        let cleaned = macAddress.trimmingCharacters(in: .whitespaces).uppercased()
        guard isValidMac(cleaned) else {
            macSaveStatus = .error("Invalid format. Use AA:BB:CC:DD:EE:FF")
            return
        }
        macSaveStatus = .saving
        ref.child("users/\(uid)/profile/mac_address").setValue(cleaned) { error, _ in
            DispatchQueue.main.async {
                if let error = error {
                    macSaveStatus = .error(error.localizedDescription)
                } else {
                    savedMac = cleaned
                    macSaveStatus = .success
                    DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
                        macSaveStatus = .idle
                    }
                }
            }
        }
    }

    private func isValidMac(_ mac: String) -> Bool {
        let pattern = "^([0-9A-F]{2}:){5}[0-9A-F]{2}$"
        return mac.range(of: pattern, options: .regularExpression) != nil
    }

    private func changePassword() {
        guard !newPassword.isEmpty, !confirmNewPassword.isEmpty else {
            passwordChangeStatus = .error("Please fill in both fields.")
            return
        }
        guard newPassword == confirmNewPassword else {
            passwordChangeStatus = .error("Passwords do not match.")
            return
        }
        guard newPassword.count >= 6 else {
            passwordChangeStatus = .error("Minimum 6 characters.")
            return
        }
        passwordChangeStatus = .saving
        Auth.auth().currentUser?.updatePassword(to: newPassword) { error in
            DispatchQueue.main.async {
                if let error = error {
                    passwordChangeStatus = .error(error.localizedDescription)
                } else {
                    newPassword = ""
                    confirmNewPassword = ""
                    passwordChangeStatus = .success
                    DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
                        passwordChangeStatus = .idle
                    }
                }
            }
        }
    }

    private func signOut() {
        try? Auth.auth().signOut()
        dismiss()
    }

    private func deleteAccount() {
        guard let user = Auth.auth().currentUser else { return }

        // 1. Delete all user data from Realtime Database
        ref.child("users/\(uid)").removeValue { error, _ in
            if let error = error {
                DispatchQueue.main.async {
                    deleteError = "DB cleanup failed: \(error.localizedDescription)"
                }
                return
            }

            // 2. Delete the Firebase Auth account
            user.delete { error in
                DispatchQueue.main.async {
                    if let error = error {
                        deleteError = error.localizedDescription
                    } else {
                        dismiss()
                    }
                }
            }
        }
    }
}

// ============================================================
// MAC ADDRESS AUTO-FORMAT FIELD
// ============================================================
struct MacAddressField: View {
    @Binding var text: String

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "network")
                .foregroundStyle(.white.opacity(0.5))
                .frame(width: 20)

            TextField("AA:BB:CC:DD:EE:FF", text: $text)
                .foregroundStyle(.white)
                .tint(.cyan)
                .keyboardType(.asciiCapable)
                .textInputAutocapitalization(.characters)
                .onChange(of: text) { _, newValue in
                    text = formatMac(newValue)
                }

            if text.count == 17 {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(.green)
                    .font(.system(size: 15))
                    .transition(.scale.combined(with: .opacity))
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 14)
        .background(.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(
                    text.count == 17 ? Color.green.opacity(0.5) : Color.white.opacity(0.12),
                    lineWidth: 1
                )
        )
        .animation(.easeInOut(duration: 0.2), value: text.count == 17)
    }

    /// Strips non-hex chars, uppercases, auto-inserts colons every 2 chars
    private func formatMac(_ input: String) -> String {
        let hex = input
            .uppercased()
            .filter { $0.isHexDigit }
            .prefix(12)

        var result = ""
        for (index, char) in hex.enumerated() {
            if index > 0 && index % 2 == 0 {
                result += ":"
            }
            result.append(char)
        }
        return result
    }
}
