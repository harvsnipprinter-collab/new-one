// SculptForge 3D - value types for UI / render state (persisted inside the project and in UserDefaults)
import Foundation
import SwiftUI
import UIKit
import simd

enum Tool: String, Codable, CaseIterable {
    case sculpt = "Sculpt"
    case transform = "Transform"
}

enum TransformMode: String, Codable, CaseIterable {
    case move = "Move"
    case rotate = "Rotate"
    case scale = "Scale"
}

enum ShadingMode: Int, Codable, CaseIterable, Identifiable {
    case pbr = 0, matcap = 1, flat = 2, faceOrientation = 3
    var id: Int { rawValue }
    var title: String {
        switch self {
        case .pbr: return "PBR"
        case .matcap: return "Matcap"
        case .flat: return "Flat"
        case .faceOrientation: return "Face side"
        }
    }
}

struct LightSetting: Codable, Equatable, Identifiable {
    var id = UUID()
    var enabled = true
    var isPoint = false          // false = directional
    var azimuth: Float = 0.6     // radians around Y
    var elevation: Float = 0.7   // radians above the horizon
    var distance: Float = 6      // point lights only
    var color = SIMD3<Float>(1, 1, 1)
    var intensity: Float = 3

    var direction: SIMD3<Float> { // unit vector pointing from the scene towards the light
        SIMD3<Float>(cos(elevation) * sin(azimuth), sin(elevation), cos(elevation) * cos(azimuth))
    }
}

struct RenderSettings: Codable, Equatable {
    var shading: ShadingMode = .pbr
    var lights: [LightSetting] = RenderSettings.studioLights
    var ambientSky = SIMD3<Float>(0.55, 0.6, 0.7)
    var ambientGround = SIMD3<Float>(0.18, 0.16, 0.15)
    var ambientIntensity: Float = 0.6
    var exposure: Float = 1.0
    var backgroundTop = SIMD3<Float>(0.16, 0.17, 0.2)
    var backgroundBottom = SIMD3<Float>(0.05, 0.05, 0.06)
    var showGrid = true
    var showWireframe = false
    var showMask = true
    var showBrushCursor = true

    static var studioLights: [LightSetting] {
        [LightSetting(azimuth: 0.7, elevation: 0.8, color: SIMD3<Float>(1, 0.97, 0.92), intensity: 3.2),
         LightSetting(azimuth: -1.2, elevation: 0.3, color: SIMD3<Float>(0.7, 0.8, 1), intensity: 1.2),
         LightSetting(azimuth: 3.3, elevation: 0.5, color: SIMD3<Float>(1, 1, 1), intensity: 1.6)]
    }
    static var rimLights: [LightSetting] {
        [LightSetting(azimuth: 0.2, elevation: 0.4, color: SIMD3<Float>(1, 1, 1), intensity: 1.2),
         LightSetting(azimuth: 2.6, elevation: 0.5, color: SIMD3<Float>(1, 0.85, 0.7), intensity: 4.5),
         LightSetting(azimuth: -2.6, elevation: 0.5, color: SIMD3<Float>(0.6, 0.75, 1), intensity: 4.5)]
    }
    static var softLights: [LightSetting] {
        [LightSetting(azimuth: 0.0, elevation: 1.2, color: SIMD3<Float>(1, 1, 1), intensity: 2.0),
         LightSetting(azimuth: 1.6, elevation: 0.2, color: SIMD3<Float>(1, 1, 1), intensity: 0.8),
         LightSetting(azimuth: -1.6, elevation: 0.2, color: SIMD3<Float>(1, 1, 1), intensity: 0.8)]
    }
}

struct MaterialPreset: Identifiable {
    let id = UUID()
    let name: String
    let color: SIMD3<Float>
    let roughness: Float
    let metallic: Float
    let opacity: Float
    let emission: Float

    static let all: [MaterialPreset] = [
        MaterialPreset(name: "Matte clay", color: SIMD3<Float>(0.62, 0.45, 0.36), roughness: 0.9, metallic: 0, opacity: 1, emission: 0),
        MaterialPreset(name: "Glossy plastic", color: SIMD3<Float>(0.8, 0.1, 0.1), roughness: 0.18, metallic: 0, opacity: 1, emission: 0),
        MaterialPreset(name: "Painted vinyl", color: SIMD3<Float>(0.9, 0.75, 0.2), roughness: 0.4, metallic: 0, opacity: 1, emission: 0),
        MaterialPreset(name: "Metal", color: SIMD3<Float>(0.56, 0.57, 0.58), roughness: 0.35, metallic: 1, opacity: 1, emission: 0),
        MaterialPreset(name: "Gold", color: SIMD3<Float>(1.0, 0.77, 0.34), roughness: 0.22, metallic: 1, opacity: 1, emission: 0),
        MaterialPreset(name: "Silver", color: SIMD3<Float>(0.97, 0.96, 0.92), roughness: 0.18, metallic: 1, opacity: 1, emission: 0),
        MaterialPreset(name: "Ceramic", color: SIMD3<Float>(0.93, 0.93, 0.9), roughness: 0.12, metallic: 0, opacity: 1, emission: 0),
        MaterialPreset(name: "Rubber", color: SIMD3<Float>(0.05, 0.05, 0.05), roughness: 0.95, metallic: 0, opacity: 1, emission: 0),
        MaterialPreset(name: "Glass", color: SIMD3<Float>(0.9, 0.95, 1.0), roughness: 0.05, metallic: 0, opacity: 0.3, emission: 0),
        MaterialPreset(name: "Skin", color: SIMD3<Float>(0.87, 0.6, 0.48), roughness: 0.55, metallic: 0, opacity: 1, emission: 0),
        MaterialPreset(name: "Fabric", color: SIMD3<Float>(0.2, 0.25, 0.45), roughness: 0.85, metallic: 0, opacity: 1, emission: 0),
        MaterialPreset(name: "Stone", color: SIMD3<Float>(0.45, 0.44, 0.42), roughness: 0.75, metallic: 0, opacity: 1, emission: 0),
        MaterialPreset(name: "Glow", color: SIMD3<Float>(0.3, 0.9, 1.0), roughness: 0.5, metallic: 0, opacity: 1, emission: 2.0)
    ]
}

/// A user-saved brush configuration (all values the engine brush struct carries, in Codable form).
struct BrushPreset: Codable, Identifiable, Equatable {
    var id = UUID()
    var name: String
    var type: Int32
    var radius: Float
    var strength: Float
    var falloff: Int32
    var spacing: Float
    var smoothing: Float
    var frontFacesOnly: Bool
    var pressureStrength: Bool
    var pressureRadius: Bool
    var layerHeight: Float
}

struct AppSettings: Codable, Equatable {
    var fingerSculpts = true          // false: fingers only navigate, Apple Pencil sculpts
    var leftHanded = false
    var pressureGamma: Float = 1.0    // < 1 = softer response, > 1 = firmer
    var historySteps: Int = 60
    var autosaveSeconds: Double = 45
    var millimetresPerUnit: Float = 20
}

/// Everything saved into the project's app-state blob.
/// Screen-space guide image. Stored in the project's app-state blob, not as a mesh.
/// Coordinates and size are relative to the viewport so layout changes are supported.
struct ReferenceImageAsset: Codable, Identifiable {
    var id = UUID()
    var name: String
    var imageData: Data
    var x: Double = 0
    var y: Double = 0
    var size: Double = 0.48
    var angle: Double = 0
    var opacity: Double = 0.65
    var visible: Bool = true
    var locked: Bool = false
}

struct ProjectAppState: Codable {
    var render = RenderSettings()
    var cameraTarget = SIMD3<Float>(0, 0, 0)
    var cameraDistance: Float = 5
    var cameraYaw: Float = 0.5
    var cameraPitch: Float = 0.3
    var cameraFov: Float = 40
    var cameraOrtho = false
    var bookmarks: [CameraBookmark] = []
    var brushPresets: [BrushPreset] = []
    var millimetresPerUnit: Float = 20
    /// Optional for backward compatibility with older .sculptforge projects.
    var referenceImages: [ReferenceImageAsset]? = nil
}

struct CameraBookmark: Codable, Identifiable, Equatable {
    var id = UUID()
    var name: String
    var target: SIMD3<Float>
    var distance: Float
    var yaw: Float
    var pitch: Float
}

// MARK: - colour helpers (engine colours are linear RGB, pickers work in sRGB)

func srgbToLinear(_ v: Float) -> Float { v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4) }
func linearToSrgb(_ v: Float) -> Float { v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1 / 2.4) - 0.055 }

extension Color {
    init(linear c: SIMD3<Float>) {
        self.init(.sRGB, red: Double(linearToSrgb(max(0, min(1, c.x)))), green: Double(linearToSrgb(max(0, min(1, c.y)))),
                  blue: Double(linearToSrgb(max(0, min(1, c.z)))), opacity: 1)
    }
    var linearRGB: SIMD3<Float> {
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        UIColor(self).getRed(&r, green: &g, blue: &b, alpha: &a)
        return SIMD3<Float>(srgbToLinear(Float(max(0, min(1, r)))), srgbToLinear(Float(max(0, min(1, g)))),
                            srgbToLinear(Float(max(0, min(1, b)))))
    }
}
