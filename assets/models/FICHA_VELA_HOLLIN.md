# Vela y Hollín — ficha para modelado 3D

Las dos son **personajes femeninos** y las dos formas (luz y sombra) de la misma protagonista.

## Vela (forma luz)

Protagonista. Archivo base: `vela.glb` (blockout procedural, pose A, ~37k triángulos, 120 mallas).
Úsalo como referencia de proporciones, colores y piezas. La malla final debe rehacerse como malla continua, skineada y con topología limpia.

## Concepto
La última farolera de una ciudad que se apaga. Perezosa y sarcástica, pero nunca se salta una farola.
Su espada es la bombilla de una farola forjada en forma de hoja. Las venas moradas del suelo son la oscuridad que entra donde ya no hay luz.

## Escala y ejes
- Unidades en metros. Y hacia arriba, mirando a +Z. "L" es el lado izquierdo del personaje (+X).
- Altura: 1,74 m hasta la coronilla y unos 1,89 m contando las puntas del pelo. Proporción de unas 6,3 cabezas (estilo anime).
- Cuerpo femenino estilizado: hombros estrechos (X ±0,166), cintura marcada (r 0,122 en 1,05 m), caderas algo más anchas, brazos y piernas finos, pecho discreto bajo la camisa (`Chest_L/R`) y barbilla afilada.
- Articulaciones (Y en el mundo): suelo 0 · tobillo 0,08 · rodilla 0,48 · cadera 0,90 (X ±0,094) · pelvis 0,92 · cintura 1,02 · hombro 1,375 (X ±0,166) · base del cuello 1,45 · centro de la cabeza 1,62.

## Jerarquía (lista para usar como esqueleto)
`Vela > Hips > Spine > Neck > Head > {Face, Hair > Ahoge}`
`Spine > Shoulder_L/R > UpperArm_L/R > LowerArm_L/R > Hand_L/R`
`Hips > UpperLeg_L/R > LowerLeg_L/R > Foot_L/R`
`Spine > Scarf > Scarf_Tail_Front, Scarf_Tail_Back` (huesos secundarios para física y spring bones)
`Hips > Lantern` (balanceo físico)
`Prop_Sword` es independiente; su origen está en el centro de la empuñadura y la hoja apunta a +Y.
Cada grupo tiene el pivote en su articulación. Se recomienda mapearlo a un rig humanoide (Mixamo, Unity Humanoid o Godot SkeletonProfileHumanoid) con los mismos nombres.

## Paleta (materiales con nombre en el .glb)
| Material | Hex | Uso |
|---|---|---|
| Coat_White | #EDE8DF | Abrigo (blanco roto cálido, nunca blanco puro) |
| Gold_Trim | #D9A441 | Ribetes, hebillas, anillas |
| Gold_Dark | #A8752A | Guarda de la espada, farol, franja del bajo |
| Scarf_Teal | #1FA39A | Bufanda, el acento de color principal |
| Scarf_Glow | #7FF7EA | Puntas emisivas de la bufanda |
| Hair | #F2D27A | Pelo |
| Skin | #F6D9C4 | Piel |
| Shirt_Dark | #2E3347 | Camisa interior |
| Pants | #3A3F55 | Pantalón |
| Boots | #6B4A35 | Botas gastadas |
| Belt_Leather | #4E3324 | Cinturón, brazal, vuelta de bota |
| Light_Emissive | #FFE7A3 | Hoja y núcleo del farol (emisión #FFD27A) |
| Light_Cracks | #FFD98A | Grietas de luz del brazo izquierdo (emisivas) |
| Eye_Iris | #E0922A | Iris ámbar, con anillo #9C5A16 |

## Rasgos que no se pueden perder (silueta)
1. Abrigo largo hasta la rodilla, abierto por delante en V, con vuelo en el bajo y ribete dorado en solapas y bajo (más una segunda franja fina).
2. Asimetría: manga solo en el brazo derecho, con hombrera redondeada y puño acampanado con ribete. El izquierdo va desnudo, con el borde de la manga arrancada, grietas de luz doradas en el brazo y un brazal de cuero con dos anillas.
3. Bufanda turquesa de dos vueltas con el nudo a la izquierda. Una cola cae por el pecho y otra larga ondea hacia atrás; las puntas brillan.
4. Farol pequeño colgado del cinturón en la cadera izquierda. Su brillo indica la vida del personaje.
5. Pelo rubio en puntas barridas hacia atrás, **coleta alta con lazo dorado**, patillas largas y un flequillo corto que deja ver los ojos. Tiene un mechón rebelde (ahoge) que rebota.
6. Ojos grandes ámbar con línea superior gruesa, **doble rabillo y pestaña inferior**, y dos brillos. Cejas finas algo inclinadas y media sonrisa pequeña (labios #B8606A).
7. Espada-farola: pomo dorado, empuñadura de hierro con anillas, guarda en forma de pantalla de farola y hoja-bombilla emisiva con un filamento en zigzag.

## Notas para la versión final
- La cara del blockout usa calcas planas. En la final, conviene modelar los ojos como geometría con textura o como calcas en un mesh aparte, para poder cambiar expresiones.
- Shader recomendado: toon de 3 tonos con sombras lavanda (no grises), contorno por inverted hull en un tono oscuro del color base (no negro) y luz de contorno cálida o violeta.
- Hay que corregir las normales de la cara (o usar SDF face shadow) para que la sombra no ensucie el rostro.
- El bloom debe quedarse en la hoja, el farol, las puntas de la bufanda y las grietas, con un threshold alto para que el blanco del abrigo no brille.
- Movimiento secundario: colas de la bufanda, faldón del abrigo, farol y ahoge.
- Pose idle de referencia: peso en la pierna derecha, espada apoyada en el hombro derecho con la hoja hacia atrás, mano izquierda en la cadera y cabeza girada y ladeada.
- Animaciones con personalidad: bostezo en idle; golpecito a la espada cuando parpadea; al ganar, enciende una farola y se va sin mirar; con poca vida, la espada titila y él la sacude.

---

# Hollín: forma sombra (antagonista)

Archivo: `hollin.glb` (pose A, ~38k triángulos). Usa **el mismo esqueleto y las mismas proporciones que Vela**, así que las animaciones se comparten.

## Concepto
Lo que queda cuando la vela se apaga. Si Vela es perezosa, Hollín es hiperactiva y burlona; si Vela enciende farolas, Hollín odia su zumbido y las revienta a martillazos. Encaja con la mecánica del juego: fundirse en la oscuridad y romper o apagar farolas.

## Regla de diseño: es el reflejo de Vela
Todo lo asimétrico de Vela aparece en espejo:
- Manga en el brazo **izquierdo**. El derecho va desnudo, con grietas **violeta**.
- Nudo de la bufanda y farol al lado **derecho**.
- Vela lleva la espada al hombro con la derecha; Hollín arrastra el martillo con la **izquierda**.

## Paleta (materiales con nombre en el .glb)
| Material | Hex | Uso |
|---|---|---|
| Coat_Ink | #221E2B | Abrigo negro tinta |
| Violet_Trim | #9B5CFF | Ribetes (equivale al dorado de Vela) |
| Violet_Dark | #5B3A8C | Franja del bajo, collarines, guarda |
| Scarf_Violet | #3E2163 | Bufanda |
| Scarf_Void_Glow | #C98BFF | Puntas emisivas de la bufanda |
| Hair_Ash | #D3CEE0 | Pelo ceniza (blanco, como en tu modelo original) |
| Skin_Pale | #DCCFD9 | Piel más fría y pálida |
| Shirt_Black / Pants_Dark / Boots_Dark | #100E16 / #26212F / #2C2333 | Ropa |
| Strap_Leather | #5A3B2A | Bandolera cruzada (de tu modelo original) |
| Void_Emissive | #B77CFF (emisión #A35CFF) | Cristales del martillo, anillo del farol |
| Void_Cracks | #C98BFF | Grietas del brazo derecho y de la cara |
| Eye_Sclera_Dark / Eye_Iris_Void | #231C2E / #C98BFF | Esclerótica oscura, iris violeta emisivo |
| Shadow_Crystal | #2A1E3D | Púas de sombra (hombrera, martillo) |

## Rasgos que no se pueden perder
1. Abrigo con el bajo **deshilachado en picos**, sin ribete inferior (solo la franja fina).
2. **Pelo ceniza** con las mismas puntas que Vela, **melena revuelta hasta media espalda** en lugar de coleta y **dos mechones como cuernos** en lugar del ahoge.
3. **Cara de pilla malvada, sin marcas en la piel:**
   - **Párpados caídos**: una media luna de piel tapa la parte superior del ojo, con la línea de pestañas recta e inclinada hacia la nariz y el rabillo hacia arriba (ojo de gata).
   - **Mirada de reojo**: los iris desplazados hacia un lado.
   - **Cejas asimétricas**: la izquierda arqueada y burlona, la derecha hundida hacia el centro.
   - **Sonrisa ladeada** que sube hacia un lado, con una comisura marcada y un colmillo.
   - Esclerótica oscura, iris violeta brillante y **pupila rasgada**.
4. **Púas de sombra** (3 cristales) en la hombrera izquierda.
5. **Bandolera marrón** cruzada del hombro derecho a la cadera izquierda.
6. **Farol apagado**: dentro tiene una esfera negra (un pequeño agujero negro) con un anillo violeta, que enlaza con la habilidad «Agujero negro».
7. **Martillo-farola** (`Prop_Hammer`, origen en el agarre y la cabeza hacia +Y), de unos 0,95 m:
   - Mango: tramo de poste de farola con collarines, empuñadura de cuero y una **cadena enrollada** (enlaza con «Cadenas del vacío»).
   - Pomo: un cristal de sombra.
   - Cabeza: carcasa hexagonal de farola vieja, tumbada. Lleva marco de hierro, cristales rotos que brillan en violeta, placa de golpeo en un extremo, **esquirlas de cristal de sombra** en el otro y un remate de farola arriba.

## Animación y efectos
- Idle: encorvada, cabeza baja mirando desde abajo, peso en la pierna izquierda y martillo arrastrando con la cabeza cerca del suelo. El puño derecho se cierra y se abre, impaciente.
- Los emisivos **laten** despacio, frente al parpadeo de bombilla vieja de Vela.
- Transición luz ↔ sombra: el blanco del abrigo se tiñe de tinta desde el bajo hacia arriba, las grietas cambian de brazo con un destello y la espada se deforma en martillo.
