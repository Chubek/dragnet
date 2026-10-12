// DomSymAs syntax: lookup, template formatting, operand text.
#include "sandstone.hpp"
#include <algorithm>
#include <cctype>

namespace domsymas {

const SyntaxRec *Databank::find_syntax(const std::string &name) const {
    for (auto &s : data_.syntaxes) {
        if (s.case_sensitive ? s.name == name
                             : (s.name.size() == name.size() &&
                                std::equal(s.name.begin(), s.name.end(), name.begin(),
                                           [](char a, char b) {
                                               return tolower((unsigned char)a) ==
                                                      tolower((unsigned char)b);
                                           })))
            return &s;
    }
    return nullptr;
}

// Printer template language: "{mnemonic}" plus "{op0}".."{opN}". Unknown
// placeholders are left verbatim. Reversed operand order swaps op texts.
std::string Databank::format_form(const FormRec &f, uint32_t syntax,
                                  const std::map<std::string, std::string> &op_text) const {
    const MnemonicRec *m = nullptr;
    for (auto &mm : data_.mnemonics)
        if (mm.id == f.mnemonic) { m = &mm; break; }
    std::string mn = m ? m->canonical : ("form" + std::to_string(f.id));
    const SyntaxRec *s = nullptr;
    for (auto &ss : data_.syntaxes)
        if (ss.id == syntax) { s = &ss; break; }
    std::string tmpl = s && !s->tmpl.empty() ? s->tmpl : "{mnemonic} {ops}";
    auto ops = operands_for_form(f.id);
    std::sort(ops.begin(), ops.end(), [](const OperandRec *a, const OperandRec *b) {
        return a->position < b->position;
    });
    std::vector<std::string> texts;
    for (auto *o : ops) {
        if (o->hidden) continue;
        auto it = op_text.find("op" + std::to_string(o->position));
        std::string value = it != op_text.end() ? it->second : ("op" + std::to_string(o->position));
        if (s && it != op_text.end()) {
            if (o->kind == DOMSYMAS_OP_REG) value = s->reg_prefix + value;
            else if (o->kind == DOMSYMAS_OP_IMM) value = s->imm_prefix + value;
        }
        texts.push_back(std::move(value));
    }
    if (s && s->operand_order == 1) {
        std::vector<std::string> rev(texts.rbegin(), texts.rend());
        texts.swap(rev);
    }
    std::string sep = s && !s->separator.empty() ? s->separator : ", ";
    std::string joined;
    for (size_t i = 0; i < texts.size(); i++) {
        if (i) joined += sep;
        joined += texts[i];
    }
    std::string out;
    for (size_t i = 0; i < tmpl.size();) {
        if (tmpl[i] == '{') {
            size_t j = tmpl.find('}', i);
            if (j == std::string::npos) { out += tmpl.substr(i); break; }
            std::string key = tmpl.substr(i + 1, j - i - 1);
            if (key == "mnemonic") out += mn;
            else if (key == "ops") out += joined;
            else if (key.size() > 2 && key.rfind("op", 0) == 0 &&
                     std::all_of(key.begin() + 2, key.end(), [](unsigned char c) { return std::isdigit(c); })) {
                unsigned long k = 0;
                try { k = std::stoul(key.substr(2)); } catch (...) { k = texts.size(); }
                out += k < texts.size() ? texts[k] : ("{" + key + "}");
            } else out += "{" + key + "}";
            i = j + 1;
        } else out += tmpl[i++];
    }
    return out;
}

} // namespace domsymas
