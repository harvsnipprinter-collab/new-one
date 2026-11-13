#include "sf/Sculpt.h"
#include "sf/MeshOps.h"
#include "sf/Topology.h"

namespace sf {

const char* brushName(BrushType t) {
    static const char* n[] = {"Draw", "Clay", "Clay Buildup", "Inflate", "Smooth", "Flatten", "Polish", "Scrape",
                              "Crease", "Pinch", "Move", "Elastic", "Snake Hook", "Nudge", "Drag", "Twist", "Layer",
                              "Stamp", "Mask", "Blur Mask", "Sharpen Mask", "Face Group", "Paint", "Smudge",
                              "Material Paint"};
    int i = (int)t;
    return (i >= 0 && i < (int)BrushType::Count) ? n[i] : "?";
}
bool brushIsGrab(BrushType t) { return t == BrushType::Move || t == BrushType::Elastic || t == BrushType::SnakeHook; }
bool brushDeforms(BrushType t) { return (int)t < (int)BrushType::Mask; }

float falloffWeight(Falloff f, float x) {
    if (!(x < 1.0f)) return 0.0f;
    x = std::max(x, 0.0f);
    float s = 1.0f - x;
    switch (f) {
    case Falloff::Smooth: return s * s * (3.0f - 2.0f * s);
    case Falloff::Sphere: return std::sqrt(1.0f - x * x);
    case Falloff::Linear: return s;
    case Falloff::Sharp: return s * s;
    case Falloff::Constant: return 1.0f;
    }
    return s;
}

void StrokeDiff::apply(Mesh& m, bool undo) const {
    for (size_t i = 0; i < verts.size(); ++i) {
        uint32_t v = verts[i];
        if (v >= m.vcount()) continue;
        m.pos[v] = undo ? posBefore[i] : posAfter[i];
        m.col[v] = undo ? colBefore[i] : colAfter[i];
        m.rough[v] = undo ? roughBefore[i] : roughAfter[i];
        m.metal[v] = undo ? metalBefore[i] : metalAfter[i];
        m.mask[v] = undo ? maskBefore[i] : maskAfter[i];
    }
    for (size_t i = 0; i < tris.size(); ++i)
        if (tris[i] < m.faceGroup.size()) m.faceGroup[tris[i]] = undo ? groupBefore[i] : groupAfter[i];
    m.computeNormals();
}

SculptStroke::SculptStroke(Mesh& mesh, BVH& bvh, const BrushSettings& settings) : m_(mesh), bvh_(bvh), s_(settings) {
    s_.radius = std::max(s_.radius, 1e-6f);
    s_.strength = std::clamp(s_.strength, 0.0f, 1.0f);
    s_.spacing = std::clamp(s_.spacing, 0.02f, 2.0f);
    s_.smoothing = std::clamp(s_.smoothing, 0.0f, 0.95f);
    dyn_ = s_.dyntopo && brushDeforms(s_.type) && !brushIsGrab(s_.type) && s_.type != BrushType::Layer &&
           s_.dyntopoDetail > 0;
}

void SculptStroke::buildSymmetry() {
    sym_.assign(1, Mat4());
    symDet_.assign(1, 1.0f);
    const bool axes[3] = {s_.symX, s_.symY, s_.symZ};
    for (int a = 0; a < 3; ++a) {
        if (!axes[a]) continue;
        size_t n = sym_.size();
        for (size_t i = 0; i < n; ++i) {
            Mat4 r;
            r.m[a * 5] = -1.0f;
            sym_.push_back(r * sym_[i]);
            symDet_.push_back(-symDet_[i]);
        }
    }
    int count = std::clamp(s_.radialCount, 1, 64);
    if (count > 1) {
        Vec3 axis(0, 0, 0);
        axis[std::clamp(s_.radialAxis, 0, 2)] = 1.0f;
        size_t n = sym_.size();
        for (int k = 1; k < count; ++k) {
            Mat4 rot = Mat4::trs(Vec3(0, 0, 0), Quat::axisAngle(axis, 6.2831853f * k / count), Vec3(1, 1, 1));
            for (size_t i = 0; i < n; ++i) {
                sym_.push_back(rot * sym_[i]);
                symDet_.push_back(symDet_[i]);
            }
        }
    }
}

uint32_t SculptStroke::touch(uint32_t v) {
    if (dyn_) return UINT32_MAX;
    uint32_t s = slot_[v];
    if (s != UINT32_MAX) return s;
    s = (uint32_t)touched_.size();
    slot_[v] = s;
    touched_.push_back(v);
    origPos_.push_back(m_.pos[v]); origNrm_.push_back(m_.nrm[v]); origCol_.push_back(m_.col[v]);
    origRough_.push_back(m_.rough[v]); origMetal_.push_back(m_.metal[v]); origMask_.push_back(m_.mask[v]);
    layerAmt_.push_back(0.0f);
    return s;
}

// Collect vertices in the brush sphere with their weights. view = (0,0,0) disables face filtering.
void SculptStroke::gather(Vec3 c, float r, Vec3 view, bool useMask) {
    bvh_.sphereVerts(m_, c, r, vs_);
    w_.clear();
    bool filter = s_.frontFacesOnly && length2(view) > 0;
    bool elastic = s_.type == BrushType::Elastic;
    size_t out = 0;
    for (uint32_t v : vs_) {
        if (filter && dot(m_.nrm[v], view) > 0.2f) continue;
        float d = length(m_.pos[v] - c);
        float w;
        if (elastic) { // wide regularised kernel, faded out towards the gather radius
            float x = d / (r / 2.5f);
            float fade = clamp01((1.0f - d / r) / 0.4f);
            w = std::pow(1.0f + x * x, -1.5f) * fade * fade * (3.0f - 2.0f * fade);
        } else {
            w = falloffWeight(s_.falloff, d / r);
        }
        if (useMask) w *= 1.0f - clamp01(m_.mask[v]);
        if (w <= 0) continue;
        vs_[out++] = v;
        w_.push_back(w);
    }
    vs_.resize(out);
}

void SculptStroke::updateNormals(const std::vector<uint32_t>& verts) {
    if (nstamp_.size() != m_.vcount()) { nstamp_.assign(m_.vcount(), 0); ngen_ = 0; }
    if (++ngen_ == 0) { std::fill(nstamp_.begin(), nstamp_.end(), 0u); ngen_ = 1; }
    ring_.clear();
    for (uint32_t v : verts)
        for (uint32_t i = m_.vtStart[v]; i < m_.vtStart[v + 1]; ++i) {
            const uint32_t* t = &m_.tri[(size_t)m_.vtList[i] * 3];
            for (int k = 0; k < 3; ++k)
                if (nstamp_[t[k]] != ngen_) { nstamp_[t[k]] = ngen_; ring_.push_back(t[k]); }
        }
    moved_.insert(moved_.end(), verts.begin(), verts.end());
    for (uint32_t v : ring_) {
        m_.computeNormalFor(v);
        dirtyMin_ = std::min(dirtyMin_, v);
        dirtyMax_ = std::max(dirtyMax_, v);
    }
}

void SculptStroke::refitMoved() {
    if (topoChanged_ && dyn_) bvh_.refit(m_); // topology strokes rebuild the tree per dab anyway
    else bvh_.refitVerts(m_, moved_);
    moved_.clear();
}

SculptStroke::Dab SculptStroke::makeDab(const RayHit& h, Vec3 rayDir, float pressure, Vec3 delta) const {
    float p = std::clamp(pressure, 0.0f, 1.0f);
    Dab d;
    d.center = h.point; d.normal = h.normal; d.view = rayDir; d.delta = delta;
    d.radius = s_.radius * (s_.pressureRadius ? 0.3f + 0.7f * p : 1.0f);
    d.strength = s_.strength * (s_.pressureStrength ? p : 1.0f);
    return d;
}

void SculptStroke::runDynTopo(const Dab& d) {
    size_t n = 0;
    for (size_t i = 0; i < sym_.size(); ++i) {
        Vec3 c = sym_[i].dir(d.center);
        if (s_.dyntopoMode != DynTopoMode::Collapse) n += splitLongEdges(m_, c, d.radius, s_.dyntopoDetail);
        if (s_.dyntopoMode != DynTopoMode::Subdivide) n += collapseShortEdges(m_, c, d.radius, s_.dyntopoDetail * 0.45f);
    }
    if (n) {
        topoChanged_ = true;
        changed_ = true;
        m_.ensureAttributes();
        bvh_.build(m_);
    }
}

void SculptStroke::applyDab(const Dab& d) {
    if (dyn_) runDynTopo(d);
    for (size_t i = 0; i < sym_.size(); ++i) {
        Dab e = d;
        e.center = sym_[i].dir(d.center);
        e.normal = sym_[i].dir(d.normal);
        e.view = sym_[i].dir(d.view);
        e.delta = sym_[i].dir(d.delta);
        applyOne(e, symDet_[i]);
    }
    ++dabs_;
}

void SculptStroke::applyOne(const Dab& d, float det) {
    const BrushType type = s_.type;
    const float dir = s_.invert ? -1.0f : 1.0f;
    const float rate = s_.spacing / 0.15f; // keeps the result independent of dab spacing
    const float amt = d.strength * d.radius * 0.12f * rate;
    const Vec3 c = d.center;

    if (type == BrushType::FaceGroup) {
        bvh_.sphereTris(c, d.radius, ts_);
        float r2 = d.radius * d.radius;
        if (triSeen_.size() != m_.tcount()) triSeen_.assign(m_.tcount(), 0);
        for (uint32_t t : ts_) {
            const uint32_t* v = &m_.tri[(size_t)t * 3];
            if (length2(m_.pos[v[0]] - c) > r2 || length2(m_.pos[v[1]] - c) > r2 || length2(m_.pos[v[2]] - c) > r2) continue;
            if (!triSeen_[t]) { triSeen_[t] = 1; triTouched_.push_back(t); triGroupBefore_.push_back(m_.faceGroup[t]); }
            m_.faceGroup[t] = s_.invert ? 0 : s_.faceGroup;
            changed_ = true;
        }
        return;
    }

    const bool maskBrush = type == BrushType::Mask || type == BrushType::MaskBlur || type == BrushType::MaskSharpen;
    gather(c, d.radius, d.view, !maskBrush);
    if (vs_.empty()) return;
    changed_ = true;
    const size_t n = vs_.size();
    for (uint32_t v : vs_) touch(v);

    // weighted surface plane under the brush
    Vec3 an(0, 0, 0), ac(0, 0, 0);
    float ws = 0;
    for (size_t i = 0; i < n; ++i) { an += m_.nrm[vs_[i]] * w_[i]; ac += m_.pos[vs_[i]] * w_[i]; ws += w_[i]; }
    an = normalize(an);
    if (length2(an) == 0) an = d.normal;
    ac = ws > 0 ? ac / ws : c;

    auto neighbourAverage = [&]() { // Jacobi targets for smoothing brushes
        tmp_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            Vec3 sum(0, 0, 0); int cnt = 0;
            forEachNeighbor(m_, vs_[i], [&](uint32_t u) { sum += m_.pos[u]; ++cnt; });
            tmp_[i] = cnt ? sum / (float)cnt : m_.pos[vs_[i]];
        }
    };
    auto tangentToCentre = [&](Vec3 p) { Vec3 t = c - p; return t - an * dot(t, an); };

    switch (type) {
    case BrushType::Draw:
        for (size_t i = 0; i < n; ++i) m_.pos[vs_[i]] += an * (amt * w_[i] * dir);
        break;
    case BrushType::Inflate:
        for (size_t i = 0; i < n; ++i) m_.pos[vs_[i]] += m_.nrm[vs_[i]] * (amt * w_[i] * dir);
        break;
    case BrushType::Clay: {
        Vec3 pp = ac + an * (d.radius * 0.15f * d.strength * dir);
        for (size_t i = 0; i < n; ++i) {
            float dist = dot(pp - m_.pos[vs_[i]], an);
            if (dist * dir > 0) m_.pos[vs_[i]] += an * (dist * w_[i] * 0.6f);
        }
        break;
    }
    case BrushType::ClayBuildup: {
        Vec3 pp = ac + an * (d.radius * 0.25f * d.strength * dir);
        for (size_t i = 0; i < n; ++i) {
            float w = std::sqrt(w_[i]);
            float dist = dot(pp - m_.pos[vs_[i]], an);
            if (dist * dir > 0) m_.pos[vs_[i]] += an * (dist * w * 0.8f);
            m_.pos[vs_[i]] += an * (amt * 0.3f * w * dir);
        }
        break;
    }
    case BrushType::Flatten:
        for (size_t i = 0; i < n; ++i)
            m_.pos[vs_[i]] += an * (dot(ac - m_.pos[vs_[i]], an) * w_[i] * d.strength * dir);
        break;
    case BrushType::Scrape:
        for (size_t i = 0; i < n; ++i) {
            float dist = dot(ac - m_.pos[vs_[i]], an);
            if (dir > 0 ? dist < 0 : dist > 0) m_.pos[vs_[i]] += an * (dist * w_[i] * d.strength);
        }
        break;
    case BrushType::Polish:
        for (size_t i = 0; i < n; ++i)
            m_.pos[vs_[i]] += an * (dot(ac - m_.pos[vs_[i]], an) * w_[i] * d.strength * 0.5f);
        neighbourAverage();
        for (size_t i = 0; i < n; ++i) m_.pos[vs_[i]] = lerp(m_.pos[vs_[i]], tmp_[i], w_[i] * d.strength * 0.5f);
        break;
    case BrushType::Smooth: {
        // A full Laplacian step on every dab can shrink surfaces and drag vertices
        // several edge lengths. Use bounded relaxation with predominantly tangential
        // motion, keeping a small normal component to actually remove bumps.
        neighbourAverage();
        for (size_t i = 0; i < n; ++i) {
            uint32_t v = vs_[i];
            Vec3 lap = tmp_[i] - m_.pos[v];
            Vec3 normal = m_.nrm[v];
            Vec3 tangent = lap - normal * dot(lap, normal);
            Vec3 target = tangent + normal * dot(lap, normal) * 0.35f;
            float maxStep = std::max(1e-7f, d.radius * 0.06f);
            float len = length(target);
            if (len > maxStep) target = target * (maxStep / len);
            float strength = clamp01(d.strength * w_[i] * 0.35f * rate);
            m_.pos[v] += target * strength;
        }
        break;
    }
    case BrushType::Crease:
        for (size_t i = 0; i < n; ++i) {
            Vec3& p = m_.pos[vs_[i]];
            Vec3 pull = tangentToCentre(p) * (w_[i] * d.strength * 0.25f);
            p += an * (-amt * 1.2f * w_[i] * w_[i] * dir) + pull;
        }
        break;
    case BrushType::Pinch:
        for (size_t i = 0; i < n; ++i) {
            Vec3& p = m_.pos[vs_[i]];
            p += tangentToCentre(p) * (w_[i] * d.strength * 0.25f * dir);
        }
        break;
    case BrushType::Nudge: {
        Vec3 dt = d.delta - an * dot(d.delta, an);
        float k = std::min(1.0f, d.strength * 2.0f);
        for (size_t i = 0; i < n; ++i) m_.pos[vs_[i]] += dt * (w_[i] * k);
        break;
    }
    case BrushType::Drag: {
        float k = std::min(1.0f, d.strength * 2.0f);
        for (size_t i = 0; i < n; ++i) m_.pos[vs_[i]] += d.delta * (w_[i] * k);
        break;
    }
    case BrushType::Twist: {
        float angle = d.strength * 0.25f * rate * dir * det;
        for (size_t i = 0; i < n; ++i) {
            Quat q = Quat::axisAngle(an, angle * w_[i]);
            m_.pos[vs_[i]] = c + q.rotate(m_.pos[vs_[i]] - c);
        }
        break;
    }
    case BrushType::Layer:
        for (size_t i = 0; i < n; ++i) {
            uint32_t sl = slot_[vs_[i]];
            layerAmt_[sl] = std::max(layerAmt_[sl], w_[i]);
            m_.pos[vs_[i]] = origPos_[sl] + origNrm_[sl] * (s_.layerHeight * dir * layerAmt_[sl]);
        }
        break;
    case BrushType::Stamp:
        for (size_t i = 0; i < n; ++i) m_.pos[vs_[i]] += an * (d.strength * d.radius * 0.5f * w_[i] * dir);
        break;
    case BrushType::Mask:
        for (size_t i = 0; i < n; ++i)
            m_.mask[vs_[i]] = clamp01(m_.mask[vs_[i]] + dir * w_[i] * d.strength * 0.6f * rate);
        break;
    case BrushType::MaskBlur: {
        tmpF_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            float sum = m_.mask[vs_[i]]; int cnt = 1;
            forEachNeighbor(m_, vs_[i], [&](uint32_t u) { sum += m_.mask[u]; ++cnt; });
            tmpF_[i] = sum / cnt;
        }
        for (size_t i = 0; i < n; ++i) m_.mask[vs_[i]] = lerp(m_.mask[vs_[i]], tmpF_[i], w_[i] * d.strength);
        break;
    }
    case BrushType::MaskSharpen:
        for (size_t i = 0; i < n; ++i) {
            float& mk = m_.mask[vs_[i]];
            mk = clamp01(0.5f + (mk - 0.5f) * (1.0f + w_[i] * d.strength));
        }
        break;
    case BrushType::Paint: {
        Vec3 target = s_.invert ? Vec3(1, 1, 1) : s_.color;
        for (size_t i = 0; i < n; ++i) {
            float a = clamp01(w_[i] * d.strength * 0.5f * rate);
            m_.col[vs_[i]] = lerp(m_.col[vs_[i]], target, a);
        }
        break;
    }
    case BrushType::Smudge: {
        Vec3 avg(0, 0, 0);
        for (size_t i = 0; i < n; ++i) avg += m_.col[vs_[i]] * w_[i];
        avg = ws > 0 ? avg / ws : avg;
        if (!smudgeValid_) { smudge_ = avg; smudgeValid_ = true; break; }
        smudge_ = lerp(smudge_, avg, 0.3f);
        for (size_t i = 0; i < n; ++i) m_.col[vs_[i]] = lerp(m_.col[vs_[i]], smudge_, clamp01(w_[i] * d.strength));
        break;
    }
    case BrushType::MaterialPaint:
        for (size_t i = 0; i < n; ++i) {
            float a = clamp01(w_[i] * d.strength * 0.5f * rate);
            uint32_t v = vs_[i];
            if (s_.paintColor) m_.col[v] = lerp(m_.col[v], s_.color, a);
            if (s_.paintRough) m_.rough[v] = lerp(m_.rough[v], clamp01(s_.paintRoughness), a);
            if (s_.paintMetal) m_.metal[v] = lerp(m_.metal[v], clamp01(s_.paintMetalness), a);
        }
        break;
    default: break;
    }

    if (brushDeforms(type)) {
        updateNormals(vs_);
    } else {
        for (uint32_t v : vs_) { dirtyMin_ = std::min(dirtyMin_, v); dirtyMax_ = std::max(dirtyMax_, v); }
    }
}

void SculptStroke::applyGrab(Vec3 total) {
    float scale = std::min(1.0f, s_.strength * 2.0f);
    tmp_.assign(touched_.size(), Vec3(0, 0, 0));
    tmpF_.assign(touched_.size(), 0.0f);
    for (const GrabEntry& e : grab_) {
        uint32_t sl = slot_[e.v];
        tmp_[sl] += sym_[e.sym].dir(total) * e.w;
        tmpF_[sl] += e.w;
    }
    // overlapping symmetric copies are averaged so vertices on a mirror plane stay on it
    for (size_t i = 0; i < touched_.size(); ++i)
        m_.pos[touched_[i]] = origPos_[i] + tmp_[i] * (scale / std::max(1.0f, tmpF_[i]));
    if (!touched_.empty()) changed_ = true;
    updateNormals(touched_);
}

void SculptStroke::applyHook(Vec3 from, Vec3 step, float radius) {
    float scale = std::min(1.0f, s_.strength * 2.0f);
    for (size_t i = 0; i < sym_.size(); ++i) {
        Vec3 c = sym_[i].dir(from), dl = sym_[i].dir(step);
        gather(c, radius, Vec3(0, 0, 0), true);
        if (vs_.empty()) continue;
        changed_ = true;
        for (size_t k = 0; k < vs_.size(); ++k) {
            touch(vs_[k]);
            m_.pos[vs_[k]] += dl * (w_[k] * scale);
        }
        updateNormals(vs_);
    }
    ++dabs_;
}

bool SculptStroke::begin(Vec3 o, Vec3 dirIn, float pressure) {
    Vec3 d = normalize(dirIn);
    if (m_.vtStart.size() != m_.vcount() + 1) m_.buildAdjacency();
    if (bvh_.empty()) bvh_.build(m_);
    RayHit h = bvh_.raycast(m_, o, d, false);
    if (!h.hit) return false;
    buildSymmetry();
    if (!dyn_) slot_.assign(m_.vcount(), UINT32_MAX);
    started_ = true;
    smoothO_ = o; smoothD_ = d;
    lastPos_ = h.point; lastNormal_ = h.normal; lastRayO_ = o; lastRayD_ = d;
    if (brushIsGrab(s_.type)) {
        grabOrigin_ = grabPoint_ = h.point;
        grabView_ = d;
        if (s_.type != BrushType::SnakeHook) {
            float r = s_.radius * (s_.type == BrushType::Elastic ? 2.5f : 1.0f);
            for (size_t i = 0; i < sym_.size(); ++i) {
                gather(sym_[i].dir(h.point), r, Vec3(0, 0, 0), true);
                for (size_t k = 0; k < vs_.size(); ++k) {
                    touch(vs_[k]);
                    grab_.push_back({vs_[k], w_[k], (uint16_t)i});
                }
            }
        }
        return true;
    }
    applyDab(makeDab(h, d, pressure, Vec3(0, 0, 0)));
    if (brushDeforms(s_.type)) refitMoved();
    return true;
}

void SculptStroke::move(Vec3 o, Vec3 dirIn, float pressure) {
    if (!started_) return;
    Vec3 d = normalize(dirIn);
    if (s_.smoothing > 0) {
        smoothO_ = lerp(smoothO_, o, 1.0f - s_.smoothing);
        smoothD_ = normalize(lerp(smoothD_, d, 1.0f - s_.smoothing));
        o = smoothO_; d = smoothD_;
    }
    if (brushIsGrab(s_.type)) {
        float denom = dot(d, grabView_);
        if (std::fabs(denom) < 1e-6f) return;
        Vec3 q = o + d * (dot(grabOrigin_ - o, grabView_) / denom);
        if (!isFinite(q)) return;
        if (s_.type == BrushType::SnakeHook) {
            applyHook(grabPoint_, q - grabPoint_, s_.radius);
            grabPoint_ = q;
        } else {
            applyGrab(q - grabOrigin_);
            ++dabs_;
        }
        lastPos_ = q;
        refitMoved();
        return;
    }
    if (s_.type == BrushType::Stamp) return; // single dab at touch-down
    RayHit h = bvh_.raycast(m_, o, d, false);
    if (!h.hit) return;
    float dist = length(h.point - lastPos_);
    float step = s_.spacing * s_.radius;
    if (dist < step) return;
    int n = std::min((int)(dist / step), 400);
    Vec3 startO = lastRayO_, startD = lastRayD_;
    for (int i = 1; i <= n; ++i) {
        float t = std::min(1.0f, i * step / dist);
        Vec3 ro = lerp(startO, o, t), rd = normalize(lerp(startD, d, t));
        RayHit hi = bvh_.raycast(m_, ro, rd, false);
        if (!hi.hit) continue;
        applyDab(makeDab(hi, rd, pressure, hi.point - lastPos_));
        lastPos_ = hi.point; lastNormal_ = hi.normal; lastRayO_ = ro; lastRayD_ = rd;
    }
    if (brushDeforms(s_.type)) refitMoved();
}

void SculptStroke::end(StrokeDiff& diff) {
    diff = StrokeDiff();
    if (!started_ || dyn_ || topoChanged_) return;
    size_t n = touched_.size();
    diff.verts = touched_;
    diff.posBefore = origPos_; diff.colBefore = origCol_;
    diff.roughBefore = origRough_; diff.metalBefore = origMetal_; diff.maskBefore = origMask_;
    diff.posAfter.resize(n); diff.colAfter.resize(n);
    diff.roughAfter.resize(n); diff.metalAfter.resize(n); diff.maskAfter.resize(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t v = touched_[i];
        diff.posAfter[i] = m_.pos[v]; diff.colAfter[i] = m_.col[v];
        diff.roughAfter[i] = m_.rough[v]; diff.metalAfter[i] = m_.metal[v]; diff.maskAfter[i] = m_.mask[v];
    }
    diff.tris = triTouched_;
    diff.groupBefore = triGroupBefore_;
    diff.groupAfter.resize(triTouched_.size());
    for (size_t i = 0; i < triTouched_.size(); ++i) diff.groupAfter[i] = m_.faceGroup[triTouched_[i]];
}

} // namespace sf
