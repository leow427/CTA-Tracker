import CTAKit
import SwiftUI

/// Settings → Trackie (ecosystem spec 5.2): one Trackie's name and color, shown big here and written to every device.
struct PetSettings: View {
    @Environment(AppModel.self) private var model
    @State private var name = ""

    var body: some View {
        List {
            Section {
                PixelImage(image: Glyphs.pet(.happy, colors: model.pet.colors), points: 128)
                    .padding(Theme.Space.l)
                    .plate(Theme.cream, rim: Theme.navy, rimWidth: 3)
                    .frame(maxWidth: .infinity)
                    .listRowBackground(Color.clear)
                    .accessibilityLabel("\(model.pet.name), \(presetName)")
                    .animation(Theme.snappy, value: model.pet.colors)
            }
            Section("Name") {
                TextField("Trackie", text: $name)
                    .autocorrectionDisabled()
                    .submitLabel(.done)
            }
            Section {
                HStack(spacing: 0) {
                    ForEach(Pet.presets, id: \.self) { preset in
                        swatch(preset).frame(maxWidth: .infinity)
                    }
                }
                .padding(.vertical, Theme.Space.s)
            } header: {
                Text("Color")
            } footer: {
                Text("Your dock and Trackie change to match.").foregroundStyle(Theme.secondaryText)
            }
        }
        .navigationTitle("Trackie")
        .onAppear { name = model.pet.name }
        .onChange(of: name) {
            // An empty field keeps the last name: the devices always show one.
            let trimmed = name.trimmingCharacters(in: .whitespaces)
            if !trimmed.isEmpty, trimmed != model.pet.name { model.pet = PetConfig(name: trimmed, colors: model.pet.colors) }
        }
        .sensoryFeedback(.selection, trigger: model.pet.colors)
    }

    private var presetName: String { Pet.presets.first { $0.colors == model.pet.colors }?.name ?? "Custom" }

    private func swatch(_ preset: Pet.Preset) -> some View {
        let selected = preset.colors == model.pet.colors
        return Button {
            model.pet = PetConfig(name: model.pet.name, colors: preset.colors)
        } label: {
            // The body color with its light shade as a highlight, as on Trackie's roof.
            Circle()
                .fill(Color(rgb: preset.colors[0]))
                .overlay(alignment: .topLeading) {
                    Circle().fill(Color(rgb: preset.colors[1])).frame(width: 14, height: 14).offset(x: 7, y: 7)
                }
                .frame(width: 40, height: 40)
                .padding(4)
                .overlay(Circle().strokeBorder(selected ? Color.primary : .clear, lineWidth: 2.5))
                .scaleEffect(selected ? 1 : 0.92)
                .animation(Theme.snappy, value: selected)
        }
        .buttonStyle(.plain)
        .accessibilityLabel(preset.name)
        .accessibilityAddTraits(selected ? .isSelected : [])
    }
}
