#define _CRT_SECURE_NO_WARNINGS
#include "objtypes.hpp"

#include <cstdio>
#include <cstring>

#include "tinyxml2.h"

TypeTable gTypes;

const CatInfo& TypeTable::cat(int id) const {
    if (id < 0 || id >= (int)cats_.size()) id = unknownId_;
    return cats_[id];
}

int TypeTable::findCat(const std::string& name) const {
    for (int i = 0; i < (int)cats_.size(); ++i)
        if (cats_[i].name == name) return i;
    return -1;
}

int TypeTable::categoryFor(const std::string& style, int raw) const {
    for (const auto& s : styles_) {
        if (s.first != style) continue;
        if (raw >= 0 && raw < (int)s.second.size()) return s.second[raw];
        return unknownId_;
    }
    return unknownId_;
}

void TypeTable::loadDefaults() {
    cats_ = {
        { "Biped",           80, 220, 100 },
        { "Vehicle",        255, 150,  40 },
        { "Weapon",         240,  70,  70 },
        { "Equipment",       60, 200, 220 },
        { "Garbage",        150, 120,  80 },
        { "Projectile",     255, 230,  60 },
        { "Scenery",         90, 130, 240 },
        { "Machine",        180, 110, 230 },
        { "Control",        230,  90, 200 },
        { "Sound Scenery",   60, 180, 160 },
        { "Creature",       170, 220,  60 },
        { "Light Fixture",  250, 245, 180 },
        { "Placeholder",    150, 150, 160 },
        { "Crate",          205, 133,  63 },
        { "Effect Scenery", 240, 140, 180 },
        { "Terminal",       130,  90, 255 },
        { "FX Scenery",     255, 120, 150 },
        { "Giant",          200,  60, 120 },
        { "Unknown",        110, 110, 120 },
    };
    unknownId_ = findCat("Unknown");
    if (unknownId_ < 0) unknownId_ = (int)cats_.size() - 1;

    auto ids = [&](std::initializer_list<const char*> names) {
        std::vector<int> v;
        for (const char* n : names) {
            int id = findCat(n);
            v.push_back(id < 0 ? unknownId_ : id);
        }
        return v;
    };

    auto modern = ids({ "Biped","Vehicle","Weapon","Equipment","Garbage","Projectile",
                        "Scenery","Machine","Control","Sound Scenery","Creature","Effect Scenery" });
    styles_ = {
        { "HaloReach", modern },
        { "Halo4",     modern },
        { "Halo3",     modern },
        { "Halo3ODST", modern },
        { "Halo1",  ids({ "Biped","Vehicle","Weapon","Equipment","Garbage","Projectile",
                          "Scenery","Machine","Control","Light Fixture","Placeholder","Sound Scenery" }) },
        { "Halo2",  ids({ "Biped","Vehicle","Weapon","Equipment","Garbage","Projectile",
                          "Scenery","Machine","Control","Light Fixture","Placeholder","Sound Scenery",
                          "Crate","Creature" }) },
        { "HaloCampaignEvolved",
                    ids({ "Biped","Vehicle","Weapon","Equipment","Terminal","Projectile",
                          "Scenery","Machine","Control","Sound Scenery","Crate","Creature",
                          "Giant","FX Scenery" }) },
    };
}

bool TypeTable::load(const std::string& path, std::string& status) {
    using namespace tinyxml2;
    XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != XML_SUCCESS) {
        loadDefaults();
        status = status_ = "types.xml not found — using built-in defaults";
        return false;
    }
    XMLElement* root = doc.FirstChildElement("types");
    if (!root) {
        loadDefaults();
        status = status_ = "types.xml has no <types> root — using defaults";
        return false;
    }

    std::vector<CatInfo> cats;
    if (XMLElement* cs = root->FirstChildElement("categories")) {
        for (XMLElement* c = cs->FirstChildElement("category"); c; c = c->NextSiblingElement("category")) {
            CatInfo info;
            if (const char* nm = c->Attribute("name")) info.name = nm;
            if (const char* col = c->Attribute("color")) {
                unsigned r = 110, g = 110, b = 120;
                std::sscanf(col, "%u,%u,%u", &r, &g, &b);
                info.r = (unsigned char)r; info.g = (unsigned char)g; info.b = (unsigned char)b;
            }
            if (!info.name.empty()) cats.push_back(info);
        }
    }
    if (cats.empty()) {
        loadDefaults();
        status = status_ = "types.xml has no <category> entries — using defaults";
        return false;
    }

    cats_ = std::move(cats);
    unknownId_ = findCat("Unknown");
    if (unknownId_ < 0) { cats_.push_back({ "Unknown", 110, 110, 120 }); unknownId_ = (int)cats_.size() - 1; }

    styles_.clear();
    for (XMLElement* e = root->FirstChildElement("enum"); e; e = e->NextSiblingElement("enum")) {
        const char* sn = e->Attribute("name");
        if (!sn) continue;
        std::vector<int> map;
        for (XMLElement* t = e->FirstChildElement("type"); t; t = t->NextSiblingElement("type")) {
            int byte = t->IntAttribute("byte", -1);
            const char* cn = t->Attribute("category");
            if (byte < 0 || !cn) continue;
            int id = findCat(cn);
            if (id < 0) id = unknownId_;
            if ((int)map.size() <= byte) map.resize(byte + 1, unknownId_);
            map[byte] = id;
        }
        styles_.emplace_back(sn, std::move(map));
    }

    char buf[96];
    std::snprintf(buf, sizeof(buf), "loaded %d categories, %d enums from types.xml",
                  (int)cats_.size(), (int)styles_.size());
    status = status_ = buf;
    return true;
}
