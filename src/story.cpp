#include "story.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

}  // namespace

int utf8Length(const std::string& s) {
    int n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

std::string utf8Prefix(const std::string& s, int chars) {
    size_t i = 0;
    int n = 0;
    while (i < s.size()) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) {
            if (n == chars) break;
            ++n;
        }
        ++i;
    }
    return s.substr(0, i);
}

bool Script::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

bool Script::parse(const std::string& text) {
    std::istringstream in(text);
    std::string raw;
    Scene* cur = nullptr;
    while (std::getline(in, raw)) {
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("==", 0) == 0) {
            scenes_.push_back(Scene{});
            cur = &scenes_.back();
            cur->id = trim(line.substr(2));
            continue;
        }
        if (!cur) continue;
        if (line[0] == '!') {
            std::string cmd = trim(line.substr(1)), args;
            size_t sp = cmd.find(' ');
            if (sp != std::string::npos) {
                args = trim(cmd.substr(sp + 1));
                cmd = cmd.substr(0, sp);
            }
            if (cmd == "aviso" && cur->lines.empty()) cur->blocking = false;
            else if (cmd == "recuerdo" && cur->lines.empty()) cur->memory = true;
            else cur->lines.push_back({true, cmd, args});
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        cur->lines.push_back({false, trim(line.substr(0, colon)), trim(line.substr(colon + 1))});
    }
    return !scenes_.empty();
}

const Scene* Script::find(const std::string& id) const {
    for (const Scene& s : scenes_)
        if (s.id == id) return &s;
    return nullptr;
}

void Dialogue::start(const Scene* scene) {
    scene_ = scene;
    index_ = 0;
    reveal_ = 0;
    hold_ = 0;
    wait_ = 0;
}

const StoryLine* Dialogue::line() const {
    if (!scene_ || index_ >= scene_->lines.size()) return nullptr;
    const StoryLine& l = scene_->lines[index_];
    return l.command ? nullptr : &l;
}

bool Dialogue::lineComplete() const {
    const StoryLine* l = line();
    return !l || reveal_ >= float(utf8Length(l->text));
}

// Entrega las órdenes seguidas desde la posición actual hasta la próxima línea de texto.
// Una pausa detiene la entrega hasta que pasa su tiempo.
void Dialogue::skipCommands(std::vector<StoryLine>* commands) {
    while (wait_ <= 0 && scene_ && index_ < scene_->lines.size() && scene_->lines[index_].command) {
        const StoryLine& l = scene_->lines[index_];
        ++index_;
        if (l.speaker == "pausa") wait_ = float(std::atof(l.text.c_str()));
        else if (commands) commands->push_back(l);
    }
    if (wait_ <= 0 && scene_ && index_ >= scene_->lines.size()) scene_ = nullptr;
}

void Dialogue::update(float dt, std::vector<StoryLine>& commands) {
    newChars_ = 0;
    if (wait_ > 0) {
        wait_ -= dt;
        if (wait_ > 0) return;
    }
    skipCommands(&commands);
    const StoryLine* l = line();
    if (!l) return;
    int total = utf8Length(l->text);
    float before = reveal_;
    reveal_ = std::min(float(total), reveal_ + kCharsPerSecond * dt);
    newChars_ = int(reveal_) - int(before);
    // Los avisos pasan solos: el tiempo de lectura crece con la longitud.
    if (!scene_->blocking && reveal_ >= float(total)) {
        hold_ += dt;
        if (hold_ > 1.2f + float(total) * 0.035f) {
            ++index_;
            reveal_ = 0;
            hold_ = 0;
            skipCommands(&commands);
        }
    }
}

void Dialogue::advance() {
    if (!scene_) return;
    if (wait_ > 0) {  // pulsar acorta la pausa
        wait_ = 0.001f;
        return;
    }
    if (!lineComplete()) {
        const StoryLine* l = line();
        if (l) reveal_ = float(utf8Length(l->text));
        return;
    }
    ++index_;
    reveal_ = 0;
    hold_ = 0;
    if (index_ >= scene_->lines.size()) scene_ = nullptr;
}
