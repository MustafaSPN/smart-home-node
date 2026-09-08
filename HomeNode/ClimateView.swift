import SwiftUI

// Turkish labels for the AC enums (shared with the summary card)
func acModeLabel(_ m: String) -> String {
    switch m {
    case "heat": return "Heat"
    case "dry":  return "Dry"
    case "fan":  return "Fan"
    case "auto": return "Auto"
    default:     return "Cool"
    }
}

func acModeIcon(_ m: String) -> String {
    switch m {
    case "heat": return "flame"
    case "dry":  return "humidity"
    case "fan":  return "wind"
    case "auto": return "a.circle"
    default:     return "snowflake"
    }
}

func acFanLabel(_ f: String) -> String {
    switch f {
    case "low":  return "Low"
    case "med":  return "Medium"
    case "high": return "High"
    default:     return "Auto"
    }
}

// ============================================================
// CLIMATE CONTROL SHEET
// ============================================================
struct ClimateControlView: View {
    @ObservedObject var fb: FirebaseManager
    @Environment(\.dismiss) private var dismiss

    private let modes: [(String, String)] = [
        ("cool", "Cool"), ("heat", "Heat"), ("dry", "Dry"), ("fan", "Fan")
    ]
    private let fans: [(String, String)] = [
        ("auto", "Auto"), ("low", "Low"), ("med", "Medium"), ("high", "High")
    ]

    var body: some View {
        NavigationView {
            ZStack {
                Color.black.ignoresSafeArea()
                ScrollView(showsIndicators: false) {
                    VStack(spacing: 24) {
                        roomCard
                        setpointCard
                        modeCard
                        fanCard
                        powerButton
                    }
                    .padding()
                }
            }
            .navigationTitle("Climate")
            .navigationBarTitleDisplayMode(.inline)
            .toolbarColorScheme(.dark, for: .navigationBar)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .preferredColorScheme(.dark)
    }

    // Room conditions from the DHT11
    private var roomCard: some View {
        HStack(spacing: 18) {
            metric(value: fb.roomTemp.map { String(format: "%.1f", $0) } ?? "—",
                   unit: "°C", label: "Room", icon: "thermometer.medium")
            Divider().frame(height: 44).overlay(.white.opacity(0.15))
            metric(value: fb.roomHum.map { String(format: "%.0f", $0) } ?? "—",
                   unit: "%", label: "Humidity", icon: "humidity")
        }
        .padding()
        .frame(maxWidth: .infinity)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 24))
    }

    private func metric(value: String, unit: String, label: String, icon: String) -> some View {
        VStack(spacing: 4) {
            Image(systemName: icon).font(.title3).foregroundStyle(.cyan)
            HStack(alignment: .firstTextBaseline, spacing: 2) {
                Text(value).font(.title2).fontWeight(.bold)
                Text(unit).font(.caption).foregroundStyle(.secondary)
            }
            Text(label).font(.caption2).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
    }

    // Target temperature stepper
    private var setpointCard: some View {
        VStack(spacing: 14) {
            Text("Target Temperature").font(.caption).foregroundStyle(.secondary)
            HStack(alignment: .firstTextBaseline, spacing: 2) {
                Text("\(fb.acTemp)").font(.system(size: 60, weight: .heavy))
                Text("°C").font(.title3).foregroundStyle(.secondary)
            }
            HStack(spacing: 24) {
                stepButton("minus") { fb.setAcState(temp: fb.acTemp - 1) }
                stepButton("plus") { fb.setAcState(temp: fb.acTemp + 1) }
            }
        }
        .padding()
        .frame(maxWidth: .infinity)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 24))
    }

    private func stepButton(_ icon: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: icon)
                .font(.system(size: 24, weight: .bold))
                .frame(width: 60, height: 60)
                .background(.white.opacity(0.08), in: Circle())
        }
        .buttonStyle(.plain)
    }

    // Mode selection
    private var modeCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Mode").font(.caption).foregroundStyle(.secondary)
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 10), count: 4), spacing: 10) {
                ForEach(modes, id: \.0) { key, label in
                    let active = fb.acPower && fb.acMode == key
                    Button { fb.setAcState(mode: key) } label: {
                        VStack(spacing: 6) {
                            Image(systemName: acModeIcon(key)).font(.title3)
                            Text(label).font(.caption2)
                        }
                        .frame(maxWidth: .infinity).padding(.vertical, 12)
                        .background(active ? Color.cyan : Color.white.opacity(0.06),
                                    in: RoundedRectangle(cornerRadius: 14))
                        .foregroundStyle(active ? .black : .primary)
                    }
                    .buttonStyle(.plain)
                }
            }
        }
        .padding()
        .frame(maxWidth: .infinity)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 24))
    }

    // Fan speed selection
    private var fanCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Fan Speed").font(.caption).foregroundStyle(.secondary)
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 10), count: 4), spacing: 10) {
                ForEach(fans, id: \.0) { key, label in
                    let active = fb.acFan == key
                    Button { fb.setAcState(fan: key) } label: {
                        Text(label).font(.caption)
                            .frame(maxWidth: .infinity).padding(.vertical, 14)
                            .background(active ? Color.cyan : Color.white.opacity(0.06),
                                        in: RoundedRectangle(cornerRadius: 14))
                            .foregroundStyle(active ? .black : .primary)
                    }
                    .buttonStyle(.plain)
                }
            }
        }
        .padding()
        .frame(maxWidth: .infinity)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 24))
    }

    private var powerButton: some View {
        Button {
            fb.setAcState(power: !fb.acPower)
        } label: {
            HStack {
                Image(systemName: "power")
                Text(fb.acPower ? "Turn Off" : "Turn On")
            }
            .font(.headline)
            .frame(maxWidth: .infinity).padding()
            .background(fb.acPower ? Color.red.opacity(0.85) : Color.green.opacity(0.85),
                        in: RoundedRectangle(cornerRadius: 20))
            .foregroundStyle(.white)
        }
        .buttonStyle(.plain)
    }
}
