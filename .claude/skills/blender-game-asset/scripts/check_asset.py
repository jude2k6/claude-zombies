# Asset integrity auditor for the blender-game-asset skill.
#
# Run inside Blender via the blender MCP (execute_blender_code): send this file's
# contents, then call check_asset(require_rig=True) for rigged assets or
# check_asset(require_rig=False) for static props. Reads all mesh objects in the
# scene (or the current selection if anything is selected) and prints a PASS/FAIL
# report. The headline failure it guards against: a mesh object made of more than
# one connected island, i.e. a part that floats free of the main body.

import bpy, bmesh
from mathutils import Vector


def _islands(mesh):
    """Connected components of the mesh's vertices, linked by edges."""
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.ensure_lookup_table()
    seen = set()
    comps = []
    for v in bm.verts:
        if v.index in seen:
            continue
        stack = [v]
        comp = 0
        while stack:
            x = stack.pop()
            if x.index in seen:
                continue
            seen.add(x.index)
            comp += 1
            for e in x.link_edges:
                o = e.other_vert(x)
                if o.index not in seen:
                    stack.append(o)
        comps.append(comp)
    loose_verts = sum(1 for v in bm.verts if not v.link_edges)
    loose_edges = sum(1 for e in bm.edges if not e.link_faces)
    non_manifold = sum(1 for e in bm.edges if not e.is_manifold)
    tris = sum(max(0, len(f.verts) - 2) for f in bm.faces)
    bm.free()
    comps.sort(reverse=True)
    return comps, loose_verts, loose_edges, non_manifold, tris


def _world_bbox(obj):
    cs = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    mn = Vector((min(c.x for c in cs), min(c.y for c in cs), min(c.z for c in cs)))
    mx = Vector((max(c.x for c in cs), max(c.y for c in cs), max(c.z for c in cs)))
    return mn, mx


def _bbox_gap(a, b):
    """Smallest gap between two world AABBs (0 if they touch/overlap)."""
    (amn, amx), (bmn, bmx) = a, b
    g = 0.0
    for i in range(3):
        d = max(bmn[i] - amx[i], amn[i] - bmx[i], 0.0)
        g += d * d
    return g ** 0.5


def check_asset(require_rig=True):
    sel = [o for o in bpy.context.selected_objects if o.type == 'MESH']
    meshes = sel if sel else [o for o in bpy.context.scene.objects if o.type == 'MESH']
    arms = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']

    fails, warns = [], []
    print("=" * 60)
    print("ASSET INTEGRITY AUDIT  (%d mesh object%s, %d armature%s)"
          % (len(meshes), "" if len(meshes) == 1 else "s",
             len(arms), "" if len(arms) == 1 else "s"))
    print("=" * 60)

    if not meshes:
        print("FAIL: no mesh objects found")
        print("ASSET CHECK: FAIL")
        return

    # asset-wide scale for the floating-object threshold
    allmn = Vector((1e9,) * 3); allmx = Vector((-1e9,) * 3)
    boxes = {}
    for o in meshes:
        mn, mx = _world_bbox(o)
        boxes[o.name] = (mn, mx)
        for i in range(3):
            allmn[i] = min(allmn[i], mn[i]); allmx[i] = max(allmx[i], mx[i])
    diag = (allmx - allmn).length
    gap_tol = max(0.02 * diag, 0.02)  # 2% of asset size, min 2 cm

    total_tris = 0
    for o in meshes:
        comps, lv, le, nm, tris = _islands(o.data)
        total_tris += tris
        line = "  %-22s verts=%-5d tris=%-5d islands=%d" % (
            o.name, len(o.data.vertices), tris, len(comps))
        print(line)

        if len(comps) > 1:
            fails.append("%s: %d disconnected islands %s -- part(s) not "
                         "connected to the body" % (o.name, len(comps), comps[:6]))
        if lv:
            fails.append("%s: %d loose vertices (in no edge)" % (o.name, lv))
        if le:
            fails.append("%s: %d loose edges (in no face)" % (o.name, le))
        if nm:
            warns.append("%s: %d non-manifold edges" % (o.name, nm))

        # default-ish material names are a smell
        if not o.data.materials:
            warns.append("%s: no material assigned" % o.name)
        for m in o.data.materials:
            if m and m.name.startswith("Material"):
                warns.append("%s: default material name '%s'" % (o.name, m.name))

    # Floating-object check: each separate part should touch/overlap another.
    if len(meshes) > 1:
        for o in meshes:
            nearest = min(_bbox_gap(boxes[o.name], boxes[p.name])
                          for p in meshes if p is not o)
            if nearest > gap_tol:
                fails.append("%s: isolated in space (gap %.3f m > %.3f m to "
                             "nearest part) -- floating, not connected"
                             % (o.name, nearest, gap_tol))

    # Rig checks
    if require_rig:
        if not arms:
            fails.append("no armature in scene (rigged asset expected)")
        else:
            arm = arms[0]
            for o in meshes:
                bound = (o.parent == arm) or any(
                    mod.type == 'ARMATURE' and mod.object == arm
                    for mod in o.modifiers)
                if not bound:
                    fails.append("%s: not bound to armature '%s'" % (o.name, arm.name))
                    continue
                gi = {g.name: g.index for g in o.vertex_groups}
                bone_idx = {gi[b.name] for b in arm.data.bones if b.name in gi}
                unweighted = 0
                for v in o.data.vertices:
                    w = sum(g.weight for g in v.groups if g.group in bone_idx)
                    if w <= 1e-6:
                        unweighted += 1
                if unweighted:
                    fails.append("%s: %d vertices with zero bone weight"
                                 % (o.name, unweighted))
            print("  armature '%s': %d bones %s"
                  % (arm.name, len(arm.data.bones),
                     [b.name for b in arm.data.bones][:12]))

    print("-" * 60)
    print("total tris: %d   asset size: %.2f m" % (total_tris, diag))
    for w in warns:
        print("  WARN: " + w)
    for f in fails:
        print("  FAIL: " + f)
    print("=" * 60)
    print("ASSET CHECK: %s" % ("PASS" if not fails else "FAIL"))
    return not fails
