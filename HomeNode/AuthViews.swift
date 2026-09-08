import SwiftUI
import FirebaseAuth

// ============================================================
// LOGIN VIEW
// ============================================================
struct LoginView: View {
    @EnvironmentObject var authManager: AuthStateManager
    @State private var email = ""
    @State private var password = ""
    @State private var errorMessage = ""
    @State private var isLoading = false
    @State private var showRegister = false
    @State private var bgActive = false

    var body: some View {
        ZStack {
            AnimatedBackground(isActive: $bgActive)

            ScrollView(showsIndicators: false) {
                VStack(spacing: 32) {
                    Spacer().frame(height: 80)

                    // Logo + Title
                    VStack(spacing: 12) {
                        ZStack {
                            Circle()
                                .fill(.ultraThinMaterial)
                                .frame(width: 80, height: 80)
                            Image(systemName: "house.fill")
                                .font(.system(size: 36))
                                .foregroundStyle(
                                    LinearGradient(colors: [.purple, .cyan], startPoint: .topLeading, endPoint: .bottomTrailing)
                                )
                        }
                        Text("HomeNode")
                            .font(.largeTitle).fontWeight(.bold)
                            .foregroundStyle(.white)
                        Text("Sign in to continue")
                            .font(.subheadline)
                            .foregroundStyle(.white.opacity(0.6))
                    }

                    // Form Card
                    VStack(spacing: 16) {
                        HNTextField(icon: "envelope.fill", placeholder: "Email", text: $email)
                            .keyboardType(.emailAddress)
                            .textInputAutocapitalization(.never)

                        HNTextField(icon: "lock.fill", placeholder: "Password", text: $password, isSecure: true)

                        if !errorMessage.isEmpty {
                            Text(errorMessage)
                                .font(.caption)
                                .foregroundStyle(.red.opacity(0.9))
                                .multilineTextAlignment(.center)
                                .padding(.horizontal)
                        }

                        Button(action: login) {
                            HStack {
                                if isLoading {
                                    ProgressView().tint(.white)
                                } else {
                                    Text("Sign In")
                                        .fontWeight(.semibold)
                                }
                            }
                            .frame(maxWidth: .infinity, minHeight: 52)
                        }
                        .background(
                            LinearGradient(colors: [.purple, .cyan], startPoint: .leading, endPoint: .trailing),
                            in: RoundedRectangle(cornerRadius: 16)
                        )
                        .foregroundStyle(.white)
                        .disabled(isLoading)
                    }
                    .padding(24)
                    .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 24))
                    .padding(.horizontal, 24)

                    // Register Link
                    Button(action: { showRegister = true }) {
                        HStack(spacing: 4) {
                            Text("Don't have an account?")
                                .foregroundStyle(.white.opacity(0.6))
                            Text("Sign Up")
                                .fontWeight(.semibold)
                                .foregroundStyle(
                                    LinearGradient(colors: [.purple, .cyan], startPoint: .leading, endPoint: .trailing)
                                )
                        }
                        .font(.subheadline)
                    }

                    Spacer().frame(height: 40)
                }
            }
        }
        .onAppear { bgActive = true }
        .fullScreenCover(isPresented: $showRegister) {
            RegisterView().environmentObject(authManager)
        }
    }

    private func login() {
        guard !email.isEmpty, !password.isEmpty else {
            errorMessage = "Please fill in all fields."
            return
        }
        isLoading = true
        errorMessage = ""
        Auth.auth().signIn(withEmail: email, password: password) { _, error in
            DispatchQueue.main.async {
                isLoading = false
                if let error = error {
                    errorMessage = error.localizedDescription
                }
            }
        }
    }
}

// ============================================================
// REGISTER VIEW
// ============================================================
struct RegisterView: View {
    @EnvironmentObject var authManager: AuthStateManager
    @Environment(\.dismiss) var dismiss
    @State private var email = ""
    @State private var password = ""
    @State private var confirmPassword = ""
    @State private var errorMessage = ""
    @State private var isLoading = false
    @State private var bgActive = false

    var body: some View {
        ZStack {
            AnimatedBackground(isActive: $bgActive)

            ScrollView(showsIndicators: false) {
                VStack(spacing: 32) {
                    Spacer().frame(height: 60)

                    // Header
                    VStack(spacing: 12) {
                        ZStack {
                            Circle()
                                .fill(.ultraThinMaterial)
                                .frame(width: 80, height: 80)
                            Image(systemName: "person.badge.plus.fill")
                                .font(.system(size: 34))
                                .foregroundStyle(
                                    LinearGradient(colors: [.purple, .cyan], startPoint: .topLeading, endPoint: .bottomTrailing)
                                )
                        }
                        Text("Create Account")
                            .font(.largeTitle).fontWeight(.bold)
                            .foregroundStyle(.white)
                        Text("Join HomeNode today")
                            .font(.subheadline)
                            .foregroundStyle(.white.opacity(0.6))
                    }

                    // Form
                    VStack(spacing: 16) {
                        HNTextField(icon: "envelope.fill", placeholder: "Email", text: $email)
                            .keyboardType(.emailAddress)
                            .textInputAutocapitalization(.never)

                        HNTextField(icon: "lock.fill", placeholder: "Password (min 6 characters)", text: $password, isSecure: true)

                        HNTextField(icon: "lock.rotation", placeholder: "Confirm Password", text: $confirmPassword, isSecure: true)

                        if !errorMessage.isEmpty {
                            Text(errorMessage)
                                .font(.caption)
                                .foregroundStyle(.red.opacity(0.9))
                                .multilineTextAlignment(.center)
                                .padding(.horizontal)
                        }

                        Button(action: register) {
                            HStack {
                                if isLoading {
                                    ProgressView().tint(.white)
                                } else {
                                    Text("Create Account")
                                        .fontWeight(.semibold)
                                }
                            }
                            .frame(maxWidth: .infinity, minHeight: 52)
                        }
                        .background(
                            LinearGradient(colors: [.purple, .cyan], startPoint: .leading, endPoint: .trailing),
                            in: RoundedRectangle(cornerRadius: 16)
                        )
                        .foregroundStyle(.white)
                        .disabled(isLoading)
                    }
                    .padding(24)
                    .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 24))
                    .padding(.horizontal, 24)

                    Button(action: { dismiss() }) {
                        HStack(spacing: 4) {
                            Text("Already have an account?")
                                .foregroundStyle(.white.opacity(0.6))
                            Text("Sign In")
                                .fontWeight(.semibold)
                                .foregroundStyle(
                                    LinearGradient(colors: [.purple, .cyan], startPoint: .leading, endPoint: .trailing)
                                )
                        }
                        .font(.subheadline)
                    }

                    Spacer().frame(height: 40)
                }
            }
        }
        .onAppear { bgActive = true }
    }

    private func register() {
        guard !email.isEmpty, !password.isEmpty, !confirmPassword.isEmpty else {
            errorMessage = "Please fill in all fields."
            return
        }
        guard password == confirmPassword else {
            errorMessage = "Passwords do not match."
            return
        }
        guard password.count >= 6 else {
            errorMessage = "Password must be at least 6 characters."
            return
        }
        isLoading = true
        errorMessage = ""
        Auth.auth().createUser(withEmail: email, password: password) { _, error in
            DispatchQueue.main.async {
                isLoading = false
                if let error = error {
                    errorMessage = error.localizedDescription
                } else {
                    dismiss()
                }
            }
        }
    }
}

// ============================================================
// CUSTOM TEXT FIELD
// ============================================================
struct HNTextField: View {
    let icon: String
    let placeholder: String
    @Binding var text: String
    var isSecure: Bool = false

    @State private var showPassword = false

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .foregroundStyle(.white.opacity(0.5))
                .frame(width: 20)

            if isSecure && !showPassword {
                SecureField(placeholder, text: $text)
                    .foregroundStyle(.white)
                    .tint(.cyan)
            } else {
                TextField(placeholder, text: $text)
                    .foregroundStyle(.white)
                    .tint(.cyan)
            }

            if isSecure {
                Button(action: { showPassword.toggle() }) {
                    Image(systemName: showPassword ? "eye.slash.fill" : "eye.fill")
                        .foregroundStyle(.white.opacity(showPassword ? 0.8 : 0.35))
                        .font(.system(size: 15))
                        .animation(.easeInOut(duration: 0.15), value: showPassword)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 14)
        .background(.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(.white.opacity(0.12), lineWidth: 1)
        )
    }
}
