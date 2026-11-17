#pragma once

// Barks del Abuelo Candil (assets/audio/voces/abuelo/abuelo_<tipo>_0N.ogg): la voz con la que
// entra al diálogo cuando se le habla (como en Zelda); el texto es lo que dice, en inglés (el
// guion, en guion_abuelo_tts*.csv). Tres tomas de cada tipo; el efecto Sfx::Abuelo las carga
// todas en este orden (toma = tipo · 3 + n).

enum class BarkKind { Saludo, Despedida, Si, No, Hmm, Pregunta, Sorpresa, Enfado, Ay, Risa, Suspiro, Uf, Farola, Vela, Hollin, Count };

constexpr int kBarkTakes = 3;

struct Bark {
    const char* file;  // sin carpeta ni extensión
    const char* text;  // lo que dice
};

inline constexpr Bark kAbueloBarks[int(BarkKind::Count)][kBarkTakes] = {
    {{"abuelo_saludo_01", "Evening!"}, {"abuelo_saludo_02", "Well well… long time no see!"}, {"abuelo_saludo_03", "Hey there… hello hello."}},
    {{"abuelo_despedida_01", "Go with the light."}, {"abuelo_despedida_02", "Off you go… see you tomorrow."}, {"abuelo_despedida_03", "Mind the shadows."}},
    {{"abuelo_si_01", "Yes… yes."}, {"abuelo_si_02", "Uh-huh."}, {"abuelo_si_03", "That's it."}},
    {{"abuelo_no_01", "No… no."}, {"abuelo_no_02", "Out of the question."}, {"abuelo_no_03", "Bah."}},
    {{"abuelo_hmm_01", "Hmm…"}, {"abuelo_hmm_02", "Hmmm… I see."}, {"abuelo_hmm_03", "Hm. Well well."}},
    {{"abuelo_mm_pregunta_01", "Hm?"}, {"abuelo_mm_pregunta_02", "What's that you say?"}, {"abuelo_mm_pregunta_03", "Eh? What?"}},
    {{"abuelo_eh_sorpresa_01", "Oh!"}, {"abuelo_eh_sorpresa_02", "Good heavens!"}, {"abuelo_eh_sorpresa_03", "Well I never!"}},
    {{"abuelo_eh_enfado_01", "Hey! You!"}, {"abuelo_eh_enfado_02", "Now see here!"}, {"abuelo_eh_enfado_03", "The nerve!"}},
    {{"abuelo_ay_01", "Ow!"}, {"abuelo_ay_02", "Oh, my back!"}, {"abuelo_ay_03", "Whoa!"}},
    {{"abuelo_risa_01", "Heh heh heh…"}, {"abuelo_risa_02", "Ha! Ha ha…"}, {"abuelo_risa_03", "Ho ho… the things you see."}},
    {{"abuelo_suspiro_01", "Ahhh…"}, {"abuelo_suspiro_02", "Haaah… forty years lighting lamps."}, {"abuelo_suspiro_03", "Phew…"}},
    {{"abuelo_uf_01", "Oof!"}, {"abuelo_uf_02", "Oof… these old bones."}, {"abuelo_uf_03", "Up we go!"}},
    {{"abuelo_farola_01", "That lamp… it'll go out tonight."}, {"abuelo_farola_02", "There used to be light on every corner."}, {"abuelo_farola_03", "I lit that one myself!"}},
    {{"abuelo_vela_01", "Well done, lass!"}, {"abuelo_vela_02", "You're just like your teacher."}, {"abuelo_vela_03", "Easy now… the night is long."}},
    {{"abuelo_hollin_01", "You again?"}, {"abuelo_hollin_02", "Hm. Shadow eyes…"}, {"abuelo_hollin_03", "Get away from my lamp!"}},
};
