#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct CatInfo {
    std::string   name;
    unsigned char r = 110, g = 110, b = 120;
};

class TypeTable {
public:
    TypeTable() { loadDefaults(); }

    bool load(const std::string& path, std::string& status);
    void loadDefaults();

    int count() const { return (int)cats_.size(); }
    int unknownId() const { return unknownId_; }
    const CatInfo& cat(int id) const;
    const std::string& status() const { return status_; }

    int categoryFor(const std::string& style, int raw) const;

private:
    int findCat(const std::string& name) const;

    std::vector<CatInfo> cats_;
    std::string          status_ = "built-in defaults";
    int unknownId_ = 0;
    std::vector<std::pair<std::string, std::vector<int>>> styles_;
};

extern TypeTable gTypes;
