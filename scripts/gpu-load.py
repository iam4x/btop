#!/usr/bin/env python3
"""Keep the GPU busy with a headless EGL fragment shader.

Usage: uv run --with moderngl --with glcontext scripts/gpu-load.py [seconds] [iterations]
Set CC=gcc CXX=g++ if building glcontext fails on a missing distcc compiler.
More iterations per pixel means more work per frame (default 400).
"""
import struct
import sys
import time

import moderngl

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 30
iterations = int(sys.argv[2]) if len(sys.argv) > 2 else 400
ctx = moderngl.create_standalone_context(backend="egl")
print(ctx.info["GL_RENDERER"], flush=True)
prog = ctx.program(
    vertex_shader="#version 330\nin vec2 p; void main() { gl_Position = vec4(p, 0, 1); }",
    fragment_shader=f"""#version 330
out vec4 c; uniform float t;
void main() {{
    vec2 z = gl_FragCoord.xy / 2048.0; float a = t;
    for (int i = 0; i < {iterations}; i++) {{
        z = vec2(z.x * z.x - z.y * z.y + sin(a), 2.0 * z.x * z.y + cos(a)); a += 0.001;
    }}
    c = vec4(z, 0, 1);
}}""",
)
vbo = ctx.buffer(struct.pack("12f", -1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1))
vao = ctx.simple_vertex_array(prog, vbo, "p")
fbo = ctx.simple_framebuffer((2048, 2048))
fbo.use()
end, frames = time.monotonic() + seconds, 0
while time.monotonic() < end:
    prog["t"] = frames * 0.01
    vao.render()
    ctx.finish()
    frames += 1
print(f"{frames} frames in {seconds:.0f}s", flush=True)
