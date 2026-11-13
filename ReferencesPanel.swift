// SculptForge 3D — reference images imported from iOS Photos or Files.
// Stored in .sculptforge project state. The overlay never modifies the actual 3D mesh.
import SwiftUI
import PhotosUI
import UniformTypeIdentifiers
import UIKit

struct ReferencesPanel: View {
    @ObservedObject var model: SceneModel
    @State private var selectedPhoto: PhotosPickerItem?
    @State private var importingFromFiles = false

    private var selected: ReferenceImageAsset? {
        model.referenceImages.first(where: { $0.id == model.selectedReferenceID })
    }

    private func field(_ id: UUID, _ key: WritableKeyPath<ReferenceImageAsset, Double>) -> Binding<Double> {
        Binding(get: { model.referenceImages.first(where: { $0.id == id })?[keyPath: key] ?? 0 },
                set: { v in model.editReference(id) { $0[keyPath: key] = v } })
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
        PanelSection("Add an image") {
            PhotosPicker(selection: $selectedPhoto, matching: .images, photoLibrary: .shared()) {
                Label("Import from Photos", systemImage: "photo.on.rectangle.angled")
                    .frame(maxWidth: .infinity, minHeight: 42)
            }
            .buttonStyle(.borderedProminent)
            Button { importingFromFiles = true } label: {
                Label("Import from Files (JPG / PNG / HEIC)", systemImage: "folder")
                    .frame(maxWidth: .infinity, minHeight: 42)
            }
            .buttonStyle(.bordered)
            Text("References appear over the viewport and are saved inside your project. Lock a reference to sculpt through it.")
                .font(.caption2).foregroundStyle(.secondary)
        }
        PanelSection("Reference images") {
            if model.referenceImages.isEmpty {
                Text("No references yet. Add a photo to trace while sculpting.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            ForEach(model.referenceImages) { ref in
                HStack(spacing: 8) {
                    if let preview = UIImage(data: ref.imageData) {
                        Image(uiImage: preview).resizable().scaledToFill()
                            .frame(width: 48, height: 42).clipped().cornerRadius(5)
                    }
                    Button {
                        model.selectedReferenceID = ref.id
                    } label: {
                        Text(ref.name).font(.footnote).lineLimit(1)
                            .foregroundStyle(model.selectedReferenceID == ref.id ? .orange : .primary)
                    }
                    Spacer(minLength: 0)
                    Button { model.editReference(ref.id) { $0.visible.toggle() } } label: {
                        Image(systemName: ref.visible ? "eye" : "eye.slash")
                    }.accessibilityLabel(ref.visible ? "Hide reference" : "Show reference")
                    Button { model.editReference(ref.id) { $0.locked.toggle() } } label: {
                        Image(systemName: ref.locked ? "lock.fill" : "lock.open")
                    }.accessibilityLabel(ref.locked ? "Unlock reference" : "Lock reference")
                    Button(role: .destructive) { model.deleteReference(ref.id) } label: {
                        Image(systemName: "trash")
                    }.accessibilityLabel("Delete reference")
                }
            }
        }
        if let ref = selected {
            PanelSection("Adjust selected reference") {
                HStack {
                    Text("Opacity"); Slider(value: field(ref.id, \.opacity), in: 0.08...1)
                }
                HStack {
                    Text("Size"); Slider(value: field(ref.id, \.size), in: 0.12...0.95)
                }
                HStack {
                    Text("Rotate"); Slider(value: field(ref.id, \.angle), in: -180...180)
                }
                Button("Centre reference") { model.editReference(ref.id) { $0.x = 0; $0.y = 0; $0.angle = 0 } }
                    .buttonStyle(.bordered)
                Text("Drag an unlocked image to position it. Use Lock when you want touches to reach the sculpt beneath it.")
                    .font(.caption2).foregroundStyle(.secondary)
            }
        }
        }
        .onChange(of: selectedPhoto) { newValue in
            guard let item = newValue else { return }
            Task {
                do {
                    if let data = try await item.loadTransferable(type: Data.self) {
                        model.addReferenceImage(data, name: "Photo reference")
                    } else { model.message = "Could not load the selected photo." }
                } catch { model.message = "Photo import failed: \(error.localizedDescription)" }
                selectedPhoto = nil
            }
        }
        .fileImporter(isPresented: $importingFromFiles,
                      allowedContentTypes: [.image], allowsMultipleSelection: true) { result in
            switch result {
            case .success(let urls):
                for url in urls { model.importReferenceFile(url) }
            case .failure(let error): model.message = "Image import failed: \(error.localizedDescription)"
            }
        }
    }
}

struct ReferenceOverlayCanvas: View {
    @ObservedObject var model: SceneModel
    var body: some View {
        GeometryReader { size in
            ZStack {
                ForEach(model.referenceImages) { ref in
                    if ref.visible {
                        ReferenceOverlayItem(model: model, reference: ref, viewport: size.size)
                    }
                }
            }
            .frame(width: size.size.width, height: size.size.height)
        }
    }
}

private struct ReferenceOverlayItem: View {
    @ObservedObject var model: SceneModel
    let reference: ReferenceImageAsset
    let viewport: CGSize
    @GestureState private var drag: CGSize = .zero
    @State private var image: UIImage?

    var body: some View {
        Group {
            if let image = image {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
                    .frame(width: viewport.width * reference.size)
                    .overlay {
                        if model.selectedReferenceID == reference.id && !reference.locked {
                            RoundedRectangle(cornerRadius: 3).stroke(.orange, lineWidth: 2)
                        }
                    }
                    .opacity(reference.opacity)
                    .rotationEffect(.degrees(reference.angle))
                    .position(x: viewport.width * (0.5 + reference.x) + drag.width,
                              y: viewport.height * (0.5 + reference.y) + drag.height)
                    .onTapGesture { model.selectedReferenceID = reference.id }
                    .gesture(DragGesture(minimumDistance: 3)
                        .updating($drag) { value, state, _ in state = value.translation }
                        .onEnded { value in
                            guard viewport.width > 0, viewport.height > 0 else { return }
                            model.moveReference(reference.id, byX: value.translation.width / viewport.width,
                                                byY: value.translation.height / viewport.height)
                            model.selectedReferenceID = reference.id
                        })
                    .allowsHitTesting(!reference.locked)
            }
        }
        .onAppear { image = UIImage(data: reference.imageData) }
    }
}
