precision mediump float;
uniform vec2 size;
uniform float phase;
uniform sampler2D nonceTexture;

void main() {
    vec2 point = gl_FragCoord.xy / size;
    vec3 color;
    if (point.y < 0.5) {
        color = point.x < 0.5 ? vec3(1.0 - phase, 0.0, phase) : vec3(0.0, 1.0, 0.0);
    } else {
        color = point.x < 0.5 ? vec3(phase, 0.0, 1.0 - phase) : vec3(1.0, 1.0, 0.0);
    }
    if (abs(point.x - 0.5) < 0.08 && abs(point.y - 0.5) < 0.08) {
        color = vec3(1.0);
    }
    // Bind the screenshot to this run with 32 stripes encoding the full nonce.
    if (point.y < 0.08) {
        color = texture2D(nonceTexture, vec2(point.x, 0.5)).rgb;
    }
    gl_FragColor = vec4(color, 1.0);
}
