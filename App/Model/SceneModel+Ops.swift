// SculptForge 3D - scene, topology, mask and material operations exposed to the UI
import Foundation
import simd

struct PrimitiveParam {
    let name: String
    var value: Float
    let range: ClosedRange<Float>
    let isInteger: Bool
}

extension SceneModel {
    // MARK: - primitives

    /// Editable parameters for each primitive type, in the order the engine expects them.
    static func primitiveParams(_ type: Int32) -> [PrimitiveParam] {
        func f(_ n: String, _ v: Float, _ r: ClosedRange<Float>) -> PrimitiveParam { PrimitiveParam(name: n, value: v, range: r, isInteger: false) }
        func i(_ n: String, _ v: Float, _ r: ClosedRange<Float>) -> PrimitiveParam { PrimitiveParam(name: n, value: v, range: r, isInteger: true) }
        switch type {
        case 0: return [f("Radius", 1, 0.05...5), i("Segments", 64, 8...256), i("Rings", 32, 4...128)]
        case 1: return [f("Radius", 1, 0.05...5), i("Subdivisions", 5, 1...7)]
        case 2: return [f("Width", 2, 0.05...10), f("Height", 2, 0.05...10), f("Depth", 2, 0.05...10), i("Divisions", 24, 1...128)]
        case 3: return [f("Width", 2, 0.05...10), f("Height", 2, 0.05...10), f("Depth", 2, 0.05...10), i("Divisions", 32, 2...128), f("Corner radius", 0.3, 0...2)]
        case 4: return [f("Radius", 1, 0.05...5), f("Height", 2, 0.05...10), i("Sides", 64, 3...256), i("Caps (0/1)", 1, 0...1)]
        case 5: return [f("Radius", 1, 0.05...5), f("Height", 2, 0.05...10), i("Sides", 64, 3...256), i("Cap (0/1)", 1, 0...1)]
        case 6: return [f("Major radius", 1, 0.05...5), f("Minor radius", 0.35, 0.01...2), i("Major segments", 96, 8...256), i("Minor segments", 32, 4...128)]
        case 7: return [f("Width", 2, 0.05...10), f("Depth", 2, 0.05...10), i("Divisions", 48, 1...256)]
        case 8: return [f("Radius", 1, 0.05...5), i("Sides", 64, 3...256)]
        case 9: return [f("Radius", 0.5, 0.05...5), f("Height", 2, 0.1...10), i("Sides", 48, 8...256)]
        case 10: return [f("Length", 2, 0.05...10), f("Start radius", 0.25, 0.01...2), f("End radius", 0.25, 0.01...2), i("Sides", 32, 3...128)]
        case 11: return [f("Base radius", 1, 0.05...5), f("Height", 1.5, 0.05...10), i("Base sides", 4, 3...12)]
        case 12: return [f("Radius", 1, 0.05...5), f("Thickness", 0.08, 0.005...1), i("Segments", 96, 8...256), i("Profile sides", 16, 4...64)]
        case 13: return [f("Radius", 1, 0.05...5), f("Height", 2, 0...10), f("Turns", 3, 0.25...12), f("Thickness", 0.12, 0.01...1), i("Sides", 16, 3...64)]
        default: return [f("Radius", 1, 0.05...5), f("Height", 2, 0.05...10), i("Sides", 6, 3...64)]
        }
    }

    func addPrimitive(type: Int32, params: [Float]) {
        guard !busy else { return }
        endStroke()
        let id = sf_add_primitive(scene, type, params, Int32(params.count))
        if id == 0 { message = lastError } else {
            // drop new objects at the camera target so they appear where the user is looking
            let t = camera.target
            sf_object_begin_edit(scene, id)
            var p = SFObjectProps()
            if sf_object_get_props(scene, id, &p) != 0 {
                p.position = (t.x, t.y, t.z)
                sf_object_set_props(scene, id, &p)
            }
            sf_object_end_edit(scene, id)
            selection = id
        }
        afterEdit()
    }

    // MARK: - scene hierarchy

    func deleteObject(_ id: UInt32) { guard !busy else { return }; endStroke(); finish(sf_object_delete(scene, id)) }

    func duplicateObject(_ id: UInt32) {
        guard !busy else { return }
        endStroke()
        let n = sf_object_duplicate(scene, id)
        if n != 0 { selection = n } else { message = lastError }
        afterEdit()
    }

    func rename(_ id: UInt32, to name: String) {
        guard !busy, !name.isEmpty else { return }
        finish(sf_object_set_name(scene, id, name))
    }

    func setParent(_ id: UInt32, parent: UInt32) { guard !busy else { return }; finish(sf_object_set_parent(scene, id, parent)) }

    func moveObject(_ id: UInt32, by offset: Int) {
        guard !busy, let index = objects.firstIndex(where: { $0.id == id }) else { return }
        sf_object_move_index(scene, id, Int32(index + offset))
        afterEdit()
    }

    func toggleVisible(_ id: UInt32) { editProps(id) { $0.visible = $0.visible != 0 ? 0 : 1 }; markAllForUpload() }
    func toggleLocked(_ id: UInt32) { editProps(id) { $0.locked = $0.locked != 0 ? 0 : 1 } }

    /// Show only this object (or show everything again if it is already the only visible one).
    func solo(_ id: UInt32) {
        let othersVisible = objects.contains { $0.id != id && $0.props.visible != 0 }
        for o in objects { editProps(o.id) { $0.visible = (o.id == id || !othersVisible) ? 1 : 0 } }
    }

    func mergeSelected() {
        guard !busy else { return }
        var ids = Array(multiSelection)
        if ids.count < 2 { message = "Tick at least two objects in the scene list to merge."; return }
        endStroke()
        let n = sf_op_merge(scene, &ids, Int32(ids.count))
        if n != 0 {
            selection = n; multiSelection = []
            message = "Objects joined. Intersecting surfaces are still separate; open Topology → Voxel Remesh to fuse them into one smooth surface."
        } else { message = lastError }
        afterEdit()
    }

    func separateParts() {
        guard !busy, selection != 0 else { return }
        endStroke()
        if sf_op_separate(scene, selection) == 0 { message = lastError }
        afterEdit()
    }

    func frameSelection() {
        guard let o = selectedObject else { frameAll(); return }
        var a = SFAnalysis()
        var ids = [o.id]
        if sf_analyze(scene, &ids, 1, 1, 0, &a) != 0 {
            camera.frame(min: SIMD3<Float>(a.boundsMin.0, a.boundsMin.1, a.boundsMin.2), max: SIMD3<Float>(a.boundsMax.0, a.boundsMax.1, a.boundsMax.2))
        }
    }

    func frameAll() {
        var a = SFAnalysis()
        if sf_analyze(scene, nil, 0, 1, 0, &a) != 0 {
            camera.frame(min: SIMD3<Float>(a.boundsMin.0, a.boundsMin.1, a.boundsMin.2), max: SIMD3<Float>(a.boundsMax.0, a.boundsMax.1, a.boundsMax.2))
        }
    }

    // MARK: - topology

    func subdivide(smooth: Bool) {
        guard selection != 0 else { return }
        let id = selection
        runHeavy("Subdividing") { sf_op_subdivide($0, id, smooth ? 1 : 0) }
    }

    func decimate(keepFraction: Float) {
        guard let o = selectedObject else { return }
        let target = UInt32(max(4, Float(o.triangleCount) * max(0.01, min(1, keepFraction))))
        let id = o.id
        runHeavy("Decimating") { sf_op_decimate($0, id, target) }
    }

    func voxelRemesh(voxelSize: Float) {
        guard selection != 0 else { return }
        let id = selection
        runHeavy("Voxel remeshing") { sf_op_remesh($0, id, voxelSize, engineProgressCallback, nil) }
    }

    func estimateRemesh(voxelSize: Float) -> Int {
        guard !busy, selection != 0 else { return 0 }
        return Int(sf_estimate_remesh_triangles(scene, selection, voxelSize))
    }

    func smoothMesh(iterations: Int, factor: Float) {
        guard selection != 0 else { return }
        let id = selection
        runHeavy("Smoothing") { sf_op_smooth($0, id, Int32(iterations), factor) }
    }

    func quickOp(_ work: (OpaquePointer, UInt32) -> Int32) {
        guard !busy, selection != 0 else { return }
        endStroke()
        finish(work(scene, selection))
    }

    func boolean(with other: UInt32, op: Int32, voxelSize: Float) {
        guard selection != 0, other != 0, other != selection else { message = "Choose a second object for the boolean."; return }
        let a = selection
        runHeavy("Boolean") { sf_op_boolean($0, a, other, op, voxelSize, engineProgressCallback, nil) }
    }

    /// Cut the selected object with an axis-aligned plane through its bounds centre (plus an offset).
    func cutPlane(axis: Int, offset: Float, keepPositive: Bool, cap: Bool) {
        guard !busy, let o = selectedObject else { return }
        var a = SFAnalysis()
        var ids = [o.id]
        guard sf_analyze(scene, &ids, 1, 1, 0, &a) != 0 else { message = lastError; return }
        var point: [Float] = [(a.boundsMin.0 + a.boundsMax.0) / 2, (a.boundsMin.1 + a.boundsMax.1) / 2, (a.boundsMin.2 + a.boundsMax.2) / 2]
        point[axis] += offset
        var normal: [Float] = [0, 0, 0]
        normal[axis] = keepPositive ? -1 : 1 // geometry on the positive side of the normal is removed
        endStroke()
        finish(sf_op_cut_plane(scene, o.id, point, normal, cap ? 1 : 0))
    }

    // MARK: - masking

    func maskOp(_ op: Int32, amount: Float = 1) { quickOp { sf_op_mask($0, $1, op, amount) } }

    func extractMask(thickness: Float) {
        guard !busy, selection != 0 else { return }
        endStroke()
        let n = sf_op_extract_mask(scene, selection, thickness)
        if n != 0 { selection = n } else { message = lastError }
        afterEdit()
    }

    // MARK: - paint / material

    func fillColor() {
        let c: [Float] = [brush.color.0, brush.color.1, brush.color.2]
        let channels: Int32 = (brush.paintColor != 0 ? 1 : 0) | (brush.paintRough != 0 ? 2 : 0) | (brush.paintMetal != 0 ? 4 : 0)
        quickOp { sf_op_fill($0, $1, c, self.brush.paintRoughness, self.brush.paintMetalness, channels == 0 ? 1 : channels) }
    }

    func applyMaterial(_ preset: MaterialPreset) {
        guard selection != 0 else { return }
        editProps(selection) {
            $0.baseColor = (preset.color.x, preset.color.y, preset.color.z)
            $0.roughness = preset.roughness
            $0.metallic = preset.metallic
            $0.opacity = preset.opacity
            $0.emission = preset.emission
            $0.usePainted = 0
        }
    }

    // MARK: - brush presets

    func savePreset(named name: String) {
        let b = brush
        brushPresets.append(BrushPreset(name: name.isEmpty ? String(cString: sf_brush_name(b.type)) : name, type: b.type, radius: b.radius,
                                        strength: b.strength, falloff: b.falloff, spacing: b.spacing, smoothing: b.smoothing,
                                        frontFacesOnly: b.frontFacesOnly != 0, pressureStrength: b.pressureStrength != 0,
                                        pressureRadius: b.pressureRadius != 0, layerHeight: b.layerHeight))
    }

    func applyPreset(_ p: BrushPreset) {
        brush.type = p.type
        brush.radius = p.radius
        brush.strength = p.strength
        brush.falloff = p.falloff
        brush.spacing = p.spacing
        brush.smoothing = p.smoothing
        brush.frontFacesOnly = p.frontFacesOnly ? 1 : 0
        brush.pressureStrength = p.pressureStrength ? 1 : 0
        brush.pressureRadius = p.pressureRadius ? 1 : 0
        brush.layerHeight = p.layerHeight
    }

    /// Reset the brush to engine defaults but keep the tool type, colour and symmetry.
    func resetBrush() {
        var d = SFBrush()
        sf_brush_default(&d)
        d.type = brush.type
        d.color = brush.color
        d.symX = brush.symX; d.symY = brush.symY; d.symZ = brush.symZ
        d.radialCount = brush.radialCount; d.radialAxis = brush.radialAxis
        brush = d
    }

    // MARK: - 3D print helpers

    func analyze(selectedOnly: Bool, thinThresholdMm: Float) -> SFAnalysis? {
        guard !busy else { return nil }
        var a = SFAnalysis()
        var ids = [selection]
        let ok: Int32 = (selectedOnly && selection != 0)
            ? sf_analyze(scene, &ids, 1, settings.millimetresPerUnit, thinThresholdMm, &a)
            : sf_analyze(scene, nil, 0, settings.millimetresPerUnit, thinThresholdMm, &a)
        if ok == 0 { message = lastError; return nil }
        return a
    }

    /// Merge every visible object and voxel-remesh the result into one closed solid.
    func makePrintable(voxelSize: Float, completion: @escaping (Bool) -> Void) {
        let ids = objects.filter { $0.props.visible != 0 }.map { $0.id }
        guard !ids.isEmpty else { message = "Nothing visible to prepare."; completion(false); return }
        var merged: UInt32 = 0
        runHeavy("Preparing printable solid", { scene in
            var list = ids
            var target = list[0]
            if list.count > 1 {
                target = sf_op_merge(scene, &list, Int32(list.count))
                if target == 0 { return 0 }
            }
            merged = target
            return sf_op_remesh(scene, target, voxelSize, engineProgressCallback, nil)
        }, completion: { ok in
            if merged != 0 { self.selection = merged }
            completion(ok)
        })
    }
}
