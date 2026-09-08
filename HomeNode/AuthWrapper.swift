import SwiftUI
import FirebaseAuth

// ============================================================
// AUTH WRAPPER — Routes between Login and Main
// ============================================================
struct AuthWrapper: View {
    @EnvironmentObject var authManager: AuthStateManager

    var body: some View {
        if authManager.isLoggedIn {
            MainView()
                .environmentObject(authManager)
        } else {
            LoginView()
                .environmentObject(authManager)
        }
    }
}

// ============================================================
// SHARED BACKGROUND
// ============================================================
struct AnimatedBackground: View {
    @Binding var isActive: Bool

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            Circle()
                .fill(RadialGradient(
                    colors: [Color.purple.opacity(0.6), Color.purple.opacity(0.0)],
                    center: .center, startRadius: 20, endRadius: 160))
                .frame(width: 320, height: 320)
                .offset(x: isActive ? -30 : 50, y: isActive ? -80 : -180)

            Circle()
                .fill(RadialGradient(
                    colors: [Color.cyan.opacity(0.5), Color.blue.opacity(0.0)],
                    center: .center, startRadius: 10, endRadius: 140))
                .frame(width: 280, height: 280)
                .offset(x: isActive ? 80 : -60, y: isActive ? 100 : 200)

            Circle()
                .fill(RadialGradient(
                    colors: [Color.indigo.opacity(0.4), Color.indigo.opacity(0.0)],
                    center: .center, startRadius: 20, endRadius: 180))
                .frame(width: 360, height: 360)
                .offset(x: isActive ? 60 : -80, y: isActive ? -200 : 100)
        }
        .ignoresSafeArea()
        .animation(.easeInOut(duration: 8).repeatForever(autoreverses: true), value: isActive)
    }
}
