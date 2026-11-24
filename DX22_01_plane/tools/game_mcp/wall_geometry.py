"""Centre-line wall geometry for a ball with nonzero radius."""
import math


def inset_wall(wall, origin, radius):
    a, b = wall["start"], wall["end"]
    dx, dz = float(b["x"]) - float(a["x"]), float(b["z"]) - float(a["z"])
    size = math.hypot(dx, dz)
    if size <= 1e-9 or radius <= 0:
        return wall
    nx, nz = -dz / size, dx / size
    side = (origin[0] - float(a["x"])) * nx + (origin[1] - float(a["z"])) * nz
    if side < 0:
        nx, nz = -nx, -nz
    def shifted(point):
        return {**point, "x": float(point["x"]) + nx * radius, "z": float(point["z"]) + nz * radius}
    return {**wall, "start": shifted(a), "end": shifted(b)}
