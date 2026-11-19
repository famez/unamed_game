in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vLocal;
in vec3 vSize;
in vec4 vColor;
in vec4 vLightPos;
flat in vec3 vParams;
uniform vec3 uSunDir;
uniform vec3 uCameraPos;
uniform sampler2DShadow uShadowMap;
uniform int uLightCount;
uniform vec4 uLightPos[8];    // xyz, radio
uniform vec3 uLightColor[8];
uniform int uScorchCount;
uniform vec4 uScorch[8];      // xyz, radio
uniform float uScorchAmount[8];
uniform float uWorldColor;      // croma del mundo: 0 = gris y apagado, 1 = a todo color
uniform int uPaintCount;
uniform vec4 uPaint[16];        // salpicaduras de color: xyz, radio
uniform vec3 uPaintColor[16];
uniform int uZoneCount;
uniform vec4 uZone[8];          // zonas donde el color ya ha vuelto (nodos encendidos): xyz, radio
uniform float uDarkness;        // cuánto se oscurece lo que no alumbra ninguna fuente
uniform int uLitCount;
uniform vec4 uLit[24];          // fuentes de luz (farolas, nodos, faroles): xyz, radio
uniform vec4 uOutlineColor;  // a > 0: pasada de silueta (color plano, sin iluminación)
uniform float uTime;
uniform float uSeaLevel;        // el agua de los mapas con relieve (muy abajo: no hay)
out vec4 fragColor;

float shadowFactor(float ndl) {
    vec3 p = vLightPos.xyz / vLightPos.w * 0.5 + 0.5;
    if (p.z > 1.0) return 1.0;
    float bias = mix(0.0012, 0.0003, ndl);  // más sesgo en superficies rasantes
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    // Cada muestra ya filtra 2x2 por hardware (comparación + bilineal).
    float s = texture(uShadowMap, vec3(p.xy + vec2(-0.6, -0.3) * texel * 1.5, p.z - bias))
            + texture(uShadowMap, vec3(p.xy + vec2( 0.4, -0.8) * texel * 1.5, p.z - bias))
            + texture(uShadowMap, vec3(p.xy + vec2(-0.3,  0.8) * texel * 1.5, p.z - bias))
            + texture(uShadowMap, vec3(p.xy + vec2( 0.8,  0.4) * texel * 1.5, p.z - bias));
    return s * 0.25;
}

// Distancia (en píxeles) a la línea de una rejilla de paso 1, antialiasada con fwidth.
float gridLine(vec2 p) {
    vec2 g = abs(fract(p - 0.5) - 0.5) / fwidth(p);
    return 1.0 - clamp(min(g.x, g.y), 0.0, 1.0);
}

void main() {
    if (uOutlineColor.a > 0.0) {
        // Contorno: con el color oscuro por defecto, un tono oscuro del propio color (líneas de
        // color, como en el anime); con un color propio (las formas), ese color.
        bool dark = dot(uOutlineColor.rgb, vec3(1.0)) < 0.35;
        fragColor = vec4(dark ? vColor.rgb * 0.22 + uOutlineColor.rgb * 0.3 : uOutlineColor.rgb, 0.0);
        return;
    }
    bool scenery = vParams.x > 15.5;  // el escenario lleva el material + 16
    int material = int(mod(vParams.x, 16.0) + 0.5);
    vec3 n = normalize(vNormal);
    vec3 base = toLinear(vColor.rgb);
    float seamGlow = 0.0;  // juntas luminosas del suelo (emisión que alimenta el bloom)
    float sandWet = 0.0;   // arena mojada: brilla

    if (material == 3) {  // plaza: losas de 2 m nacaradas, juntas finas y cada 6 m una línea de luz
        vec2 w = vWorldPos.xz / 2.0;
        vec2 cell = floor(w), f = fract(w);
        base *= mix(0.94, 1.04, hash(cell));
        vec2 e = min(f, 1.0 - f) / fwidth(w);
        base *= mix(0.82, 1.0, clamp(min(e.x, e.y) * 0.7, 0.0, 1.0));
        vec3 neon = toLinear(vec3(0.16, 0.86, 1.0));
        float far = 1.0 - smoothstep(8.0, 28.0, length(uCameraPos - vWorldPos));
        float lineA = gridLine(vWorldPos.xz / 6.0 + 0.5) * far;
        // Anillo luminoso alrededor de la fuente que late despacio.
        float r = length(vWorldPos.xz);
        float ring = (1.0 - smoothstep(0.0, 0.06, abs(r - 3.6))) * (0.7 + 0.3 * sin(uTime * 2.0));
        float g = max(lineA * 0.6, ring);
        base = mix(base, neon * 1.2, g);
        seamGlow = g * 0.5;
    } else if (material == 8) {  // arena: ondas que deja el viento, el grano y lo mojado junto al agua
        vec2 p = vWorldPos.xz;
        float warp = fbm(p * 0.12) * 7.0;
        float ripples = sin(dot(p, vec2(0.42, 0.91)) * 4.2 + warp);
        float far = 1.0 - smoothstep(25.0, 70.0, length(uCameraPos - vWorldPos));
        base *= 1.0 + 0.07 * ripples * far * smoothstep(0.85, 1.0, n.y);
        base *= mix(0.9, 1.08, noise(p * 0.6)) * mix(0.93, 1.05, hash(floor(p * 24.0)));
        if (uSeaLevel > -100.0) {  // la franja mojada sube y baja con las olas que llegan
            float line = uSeaLevel + 0.3 + 0.16 * sin(uTime * 0.9 + p.x * 0.07 + p.y * 0.03);
            sandWet = 1.0 - smoothstep(line - 0.1, line + 0.45, vWorldPos.y);
            base = mix(base, base * vec3(0.6, 0.58, 0.56), sandWet * 0.85);
        }
    } else if (material == 5) {  // césped: manchas de ruido en dos tonos
        // Dos lecturas de ruido (no fbm): el césped ocupa media pantalla y es barato así.
        float n = 0.65 * noise(vWorldPos.xz * 0.35) + 0.35 * noise(vWorldPos.xz * 1.3);
        base *= mix(0.8, 1.15, n);
        base = mix(base, base * vec3(1.15, 1.1, 0.7), smoothstep(0.6, 0.8, noise(vWorldPos.xz * 0.5 + 7.0)));
    }
    // El color es energía: sin croma, el escenario se queda gris y los neones casi apagados.
    // Las salpicaduras que dejan los Grises al disiparse devuelven el color a su alrededor.
    float sat = uWorldColor;  // croma en este punto: el global o el de una zona cercana
    for (int i = 0; i < uZoneCount; ++i)
        sat = max(sat, 1.0 - smoothstep(uZone[i].w * 0.55, uZone[i].w, length(vWorldPos - uZone[i].xyz)));
    if (scenery) {
        float l = dot(base, vec3(0.299, 0.587, 0.114));
        base = mix(vec3(l), base, mix(0.04, 1.0, sat)) * mix(0.55, 1.0, sat);
        if (material == 4) base *= mix(0.2, 1.0, sat);
        seamGlow *= sat;
        if (n.y > 0.5) {
            for (int i = 0; i < uPaintCount; ++i) {
                vec2 d2 = vWorldPos.xz - uPaint[i].xz;
                if (abs(vWorldPos.y - uPaint[i].y) > 0.6 || dot(d2, d2) > uPaint[i].w * uPaint[i].w * 1.6) continue;
                float edge = (fbm(vWorldPos.xz * 2.5 + float(i) * 7.3) - 0.5) * uPaint[i].w * 0.7;
                float k = 1.0 - smoothstep(uPaint[i].w * 0.55, uPaint[i].w, length(d2) + edge);
                base = mix(base, toLinear(uPaintColor[i]) * 0.9, k * 0.85);
                seamGlow = max(seamGlow, k * 0.15);
            }
        }
    }
    if (material == 4) {  // emisivo: el color es luz propia, sin sombras
        vec3 Ve = normalize(uCameraPos - vWorldPos);
        float rimE = pow(1.0 - max(dot(n, Ve), 0.0), 2.0);
        // Poco más que su color: el halo lo pone el bloom, así el neón conserva el tono.
        vec3 glow = base * (1.05 + 0.35 * rimE);
        glow = mix(glow, vec3(2.0), vParams.y);
        float distE = length(uCameraPos - vWorldPos);
        vec3 fogE = mix(vec3(dot(kHorizon, vec3(0.299, 0.587, 0.114))), kHorizon, mix(0.15, 1.0, sat));
        glow = mix(glow, fogE, 1.0 - exp(-pow(distE * kWorldFog, 2.0)));
        fragColor = vec4(finish(glow), 1.0);  // alfa = máscara de bloom: brilla
        return;
    }
    // Personajes (todo lo que no es escenario): sombreado anime. Luz principal propia (entre
    // el sol y la cámara, para que siempre estén bien iluminados), dos tonos con un corte casi
    // seco y la sombra teñida (no gris), una banda cálida en el borde de la sombra, borde de luz
    // nítido de ancho constante, brillo en banda en el pelo y duro en el metal.
    if (!scenery && material != 6) {
        vec3 Vc = normalize(uCameraPos - vWorldPos);
        vec3 key = normalize(uSunDir * 0.6 + Vc * 0.55 + vec3(0.0, 0.45, 0.0));
        float ndk = dot(n, key);
        float castSh = shadowFactor(max(ndk, 0.0));
        float lit = smoothstep(0.0, 0.07, ndk) * mix(0.45, 1.0, castSh);
        vec3 color = mix(base * vec3(0.58, 0.5, 0.68), base * 1.08, lit);
        float edgeBand = smoothstep(-0.06, 0.0, ndk) * (1.0 - smoothstep(0.0, 0.07, ndk));
        color += base * vec3(0.2, 0.06, 0.04) * edgeBand;
        float fr = 1.0 - max(dot(n, Vc), 0.0);
        float rim = smoothstep(0.66, 0.7, fr) * smoothstep(-0.1, 0.25, ndk + 0.25);
        color += rim * (base * 0.55 + vec3(0.08));
        if (material == 7) {  // pelo: el «anillo» de brillo
            float hb = dot(n, normalize(Vc + vec3(0.0, 0.8, 0.0)));
            color += smoothstep(0.8, 0.84, hb) * (1.0 - smoothstep(0.9, 0.94, hb)) * lit * vec3(0.24, 0.22, 0.26);
        }
        if (material == 2)  // metal: un brillo duro
            color += step(0.93, max(dot(reflect(-key, n), Vc), 0.0)) * lit * vec3(0.7);
        for (int i = 0; i < uLightCount; ++i) {
            vec3 Lv = uLightPos[i].xyz - vWorldPos;
            float d = length(Lv);
            float att = clamp(1.0 - d / uLightPos[i].w, 0.0, 1.0);
            color += base * uLightColor[i] * att * att * 0.7 * max(dot(n, Lv / max(d, 1e-3)) * 0.5 + 0.5, 0.0);
        }
        color *= mix(0.82, 1.0, uWorldColor);
        color = mix(color, vec3(1.6), vParams.y);  // destello de impacto
        float distC = length(uCameraPos - vWorldPos);
        float fogC = 1.0 - exp(-pow(distC * kWorldFog, 2.0));
        color = mix(color, mix(vec3(dot(kHorizon, vec3(0.299, 0.587, 0.114))), kHorizon, mix(0.15, 1.0, uWorldColor)), fogC);
        fragColor = vec4(finish(color), clamp(vParams.y * 0.6, 0.0, 1.0));
        return;
    }
    if (material == 6) {  // agua: ondas que se mueven y reflejo del cielo según el ángulo
        vec2 q = vWorldPos.xz * 3.0;
        float t = uTime * 1.5;
        vec3 wn = normalize(vec3(sin(q.x + t) * 0.15 + sin(q.y * 1.3 - t * 0.8) * 0.1, 1.0,
                                 cos(q.y + t * 1.1) * 0.15 + cos(q.x * 0.7 + t) * 0.1));
        n = wn;
    }

    // Chamuscado: oscurece suelo y plataformas (caras de arriba) bajo los fuegos, con
    // borde irregular gracias al ruido.
    if ((material == 1 || material == 3 || material == 5) && n.y > 0.5) {
        for (int i = 0; i < uScorchCount; ++i) {
            if (abs(vWorldPos.y - uScorch[i].y) > 0.4) continue;
            float d = length(vWorldPos.xz - uScorch[i].xz) + (fbm(vWorldPos.xz * 2.0) - 0.5) * 1.2;
            float k = 1.0 - smoothstep(uScorch[i].w * 0.3, uScorch[i].w, d);
            base *= 1.0 - 0.85 * k * uScorchAmount[i];
        }
    }

    // Bisel: oscurece cerca de las aristas. En una cara, una de las tres distancias al
    // borde es ~0 (la del eje de la cara); la arista más cercana es la mediana.
    float edge = 1.0;
    if (material != 3 && material != 5 && material != 6 && vParams.z < 0.5) {  // los elipsoides no tienen aristas
        vec3 d = (0.5 - abs(vLocal)) * vSize;
        float mid = d.x + d.y + d.z - min(d.x, min(d.y, d.z)) - max(d.x, max(d.y, d.z));
        float width = material == 1 ? 0.06 : 0.012;
        edge = mix(0.5, 1.0, smoothstep(0.0, width, mid));
    }

    vec3 V = normalize(uCameraPos - vWorldPos);
    float ndl = max(dot(n, uSunDir), 0.0);
    float shadow = shadowFactor(ndl);
    // Sombreado de animación: la luz pasa a sombra en una franja estrecha (no a cel puro),
    // y las sombras proyectadas son más suaves y algo azuladas.
    if (material == 0) ndl = mix(ndl, smoothstep(0.05, 0.3, ndl), 0.7);
    shadow = mix(0.25, 1.0, shadow);

    // Ambiente hemisférico (cielo arriba, rebote cálido del suelo abajo) + relleno suave
    // desde el lado contrario al sol, para que las caras en sombra no queden planas.
    vec3 skyAmbient = mix(kHorizon, kZenith, 0.35) * 1.3;
    vec3 ambient = mix(kBounce * 1.6, skyAmbient, n.y * 0.5 + 0.5) * 0.5;
    vec3 fillDir = normalize(vec3(-uSunDir.x, 0.3, -uSunDir.z));
    ambient += kFillColor * max(dot(n, fillDir), 0.0);
    float specPower = material == 2 ? 80.0 : (material == 8 ? mix(12.0, 70.0, sandWet) : 20.0);
    // La tela de los cuerpos redondeados (normales suaves) brilla menos que la de las cajas.
    float specAmount = material == 2 ? 1.2 : (material == 6 ? 2.0 : (material == 3 || material == 5 ? 0.04 : (vParams.z > 0.5 ? 0.05 : 0.15)));
    if (material == 6) specPower = 120.0;
    if (material == 8) specAmount = mix(0.03, 0.5, sandWet);
    vec3 H = normalize(uSunDir + V);
    float spec = pow(max(dot(n, H), 0.0), specPower) * specAmount * step(0.0, dot(n, uSunDir));
    if (material == 8) {  // granos sueltos que destellan al sol (cerca)
        vec2 gp = floor(vWorldPos.xz * 55.0);
        float g = step(0.985, hash(gp + floor(uTime * 0.0)));
        spec += g * pow(max(dot(n, H), 0.0), 6.0) * 1.2 * (1.0 - smoothstep(6.0, 18.0, length(uCameraPos - vWorldPos)));
    }

    vec3 color = base * (ambient + kSunColor * ndl * shadow) + kSunColor * spec * shadow;
    // Luces puntuales (fuego): atenuación cuadrática hasta el radio y algo de "wrap" para
    // que la luz rodee un poco las caras.
    for (int i = 0; i < uLightCount; ++i) {
        vec3 Lv = uLightPos[i].xyz - vWorldPos;
        float d = length(Lv);
        float att = clamp(1.0 - d / uLightPos[i].w, 0.0, 1.0);
        float wrap = max(dot(n, Lv / max(d, 1e-3)) * 0.7 + 0.3, 0.0);
        color += base * uLightColor[i] * wrap * att * att;
    }
    if (material == 0 || material == 2) {  // luz de contorno: separa la silueta del fondo
        float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0);
        color += rim * kHorizon * (vParams.z > 0.5 ? 0.35 : 0.5);
    }
    if (material == 6) {  // reflejo del cielo (Fresnel)
        float fr = pow(1.0 - max(dot(n, V), 0.0), 4.0);
        color = mix(color, mix(kHorizon, kZenith, 0.3) * 1.4, 0.3 + 0.6 * fr);
    }
    // Bajo el agua: las cáusticas (la luz que las olas concentran en el fondo) y el agua, que
    // tiñe y apaga lo que tiene encima según la profundidad.
    if (scenery && uSeaLevel > -100.0 && vWorldPos.y < uSeaLevel) {
        float d = uSeaLevel - vWorldPos.y;
        vec2 cp = vWorldPos.xz * 0.8;
        float c1 = noise(cp + vec2(uTime * 0.31, uTime * 0.17)), c2 = noise(cp * 1.37 - vec2(uTime * 0.23, -uTime * 0.29));
        float caustic = pow(1.0 - abs(c1 - c2), 8.0);
        color += kSunColor * base * caustic * 1.4 * exp(-d * 0.4) * shadow * max(n.y, 0.0);
        color = mix(color, color * mix(vec3(0.7), vec3(0.45, 0.75, 0.8), uWorldColor), clamp(d * 0.3, 0.0, 0.75));
    }
    color *= edge;
    // Oscuridad: fuera del alcance de toda luz (o dentro de un foco de sombra) el escenario
    // se hunde en un violeta oscuro, con un borde fino que marca hasta dónde llega la luz
    // (por ahí no pasa la sombra fundida). Es la misma cuenta que Game::shadowAt.
    if (scenery && uDarkness > 0.0) {
        float lit = 0.0;
        for (int i = 0; i < uLitCount; ++i) {
            float dy = uLit[i].y - vWorldPos.y;
            if (dy < -1.0 || dy > 9.0) continue;
            float r = uLit[i].w;
            lit = max(lit, 1.0 - smoothstep(r * 0.6, r, length(vWorldPos.xz - uLit[i].xz)));
        }
        // Borde que respira un poco, para que la sombra parezca viva.
        float wob = (noise(vWorldPos.xz * 1.7 + uTime * 0.3) - 0.5) * 0.12;
        float dark = clamp(1.0 - lit + wob * lit * (1.0 - lit) * 4.0, 0.0, 1.0);
        vec3 night = color * vec3(0.10, 0.08, 0.17) + vec3(0.004, 0.002, 0.010);
        color = mix(color, night, dark * uDarkness);
        float band = 1.0 - smoothstep(0.0, fwidth(dark) * 1.5 + 0.02, abs(dark - 0.5));
        color += vec3(0.3, 0.12, 0.6) * band * uDarkness * 0.6;
        seamGlow = max(seamGlow, band * uDarkness * 0.5);
    }
    color = mix(color, vec3(1.6), vParams.y);  // destello de impacto

    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-pow(dist * kWorldFog, 2.0));
    vec3 fogColor = mix(vec3(dot(kHorizon, vec3(0.299, 0.587, 0.114))), kHorizon, mix(0.15, 1.0, uWorldColor));
    color = mix(color, fogColor, fog);
    // Alfa = máscara de bloom: los destellos de golpe y las juntas del suelo brillan.
    fragColor = vec4(finish(color), clamp(vParams.y * 0.6 + seamGlow * (1.0 - fog), 0.0, 1.0));
}
