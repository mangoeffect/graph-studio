#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <memory>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace nlohmann {

class json {
public:
    using object_t = std::unordered_map<std::string, json>;
    using array_t = std::vector<json>;
    using iterator = array_t::iterator;
    using const_iterator = array_t::const_iterator;

    enum class value_t {
        null,
        object,
        array,
        string,
        boolean,
        number_integer,
        number_float
    };

private:
    value_t m_type = value_t::null;
    // object/array 经 shared_ptr 间接持有：libstdc++（gcc11/ubuntu-22.04）的
    // unordered_map 不接受不完整 mapped type，而本类成员 variant 在类定义内
    // 实例化时 json 尚不完整；指针不受此限（libc++ 容忍，故 macOS 一直可编）。
    // 拷贝构造/赋值做了深拷贝以保持原先的值语义。
    using object_ptr = std::shared_ptr<object_t>;
    using array_ptr = std::shared_ptr<array_t>;
    std::variant<std::nullptr_t, bool, int64_t, double, std::string, object_ptr, array_ptr> m_value;

public:
    json() = default;
    json(std::nullptr_t) {}

    json(const json& other) : m_type(other.m_type) {
        if (m_type == value_t::object) {
            m_value = std::make_shared<object_t>(*std::get<object_ptr>(other.m_value));
        } else if (m_type == value_t::array) {
            m_value = std::make_shared<array_t>(*std::get<array_ptr>(other.m_value));
        } else {
            m_value = other.m_value;
        }
    }
    json(json&&) = default;
    json& operator=(const json& other) {
        if (this != &other) {
            json tmp(other);
            m_type = tmp.m_type;
            m_value = std::move(tmp.m_value);
        }
        return *this;
    }
    json& operator=(json&&) = default;

    json(bool value) : m_type(value_t::boolean), m_value(value) {}

    json(int value) : m_type(value_t::number_integer), m_value(static_cast<int64_t>(value)) {}
    json(long long value) : m_type(value_t::number_integer), m_value(value) {}

    json(double value) : m_type(value_t::number_float), m_value(value) {}
    json(float value) : m_type(value_t::number_float), m_value(static_cast<double>(value)) {}

    json(const std::string& value) : m_type(value_t::string), m_value(value) {}
    json(std::string&& value) : m_type(value_t::string), m_value(std::move(value)) {}
    json(const char* value) : m_type(value_t::string), m_value(std::string(value)) {}

    json(std::initializer_list<std::pair<const std::string, json>> init)
        : m_type(value_t::object), m_value(std::make_shared<object_t>()) {
        for (const auto& p : init) {
            (*this)[p.first] = p.second;
        }
    }

    json(std::initializer_list<json> init)
        : m_type(value_t::array), m_value(std::make_shared<array_t>(init.begin(), init.end())) {}

    json(const object_t& value) : m_type(value_t::object), m_value(std::make_shared<object_t>(value)) {}
    json(object_t&& value) : m_type(value_t::object), m_value(std::make_shared<object_t>(std::move(value))) {}

    json(const array_t& value) : m_type(value_t::array), m_value(std::make_shared<array_t>(value)) {}
    json(array_t&& value) : m_type(value_t::array), m_value(std::make_shared<array_t>(std::move(value))) {}

    json& operator=(std::nullptr_t) { m_type = value_t::null; m_value = nullptr; return *this; }
    json& operator=(bool value) { m_type = value_t::boolean; m_value = value; return *this; }
    json& operator=(int value) { m_type = value_t::number_integer; m_value = static_cast<int64_t>(value); return *this; }
    json& operator=(long long value) { m_type = value_t::number_integer; m_value = value; return *this; }
    json& operator=(size_t value) { m_type = value_t::number_integer; m_value = static_cast<int64_t>(value); return *this; }
    json& operator=(double value) { m_type = value_t::number_float; m_value = value; return *this; }
    json& operator=(const char* value) { m_type = value_t::string; m_value = std::string(value); return *this; }
    json& operator=(const std::string& value) { m_type = value_t::string; m_value = value; return *this; }
    json& operator=(std::string&& value) { m_type = value_t::string; m_value = std::move(value); return *this; }

    json& operator[](const std::string& key) {
        if (m_type != value_t::object) {
            m_type = value_t::object;
            m_value = std::make_shared<object_t>();
        }
        return (*std::get<object_ptr>(m_value))[key];
    }

    const json& operator[](const std::string& key) const {
        if (m_type != value_t::object) throw std::out_of_range("key not found");
        auto it = std::get<object_ptr>(m_value)->find(key);
        if (it == std::get<object_ptr>(m_value)->end()) throw std::out_of_range("key not found");
        return it->second;
    }

    json& operator[](size_t index) {
        if (m_type != value_t::array) {
            m_type = value_t::array;
            m_value = std::make_shared<array_t>();
        }
        auto& arr = *std::get<array_ptr>(m_value);
        if (index >= arr.size()) arr.resize(index + 1);
        return arr[index];
    }

    json& push_back(const json& value) {
        if (m_type != value_t::array) { m_type = value_t::array; m_value = std::make_shared<array_t>(); }
        std::get<array_ptr>(m_value)->push_back(value);
        return *this;
    }

    json& push_back(json&& value) {
        if (m_type != value_t::array) { m_type = value_t::array; m_value = std::make_shared<array_t>(); }
        std::get<array_ptr>(m_value)->push_back(std::move(value));
        return *this;
    }

    bool is_null() const { return m_type == value_t::null; }
    bool is_object() const { return m_type == value_t::object; }
    bool is_array() const { return m_type == value_t::array; }
    bool is_string() const { return m_type == value_t::string; }
    bool is_boolean() const { return m_type == value_t::boolean; }
    bool is_number_integer() const { return m_type == value_t::number_integer; }
    bool is_number_float() const { return m_type == value_t::number_float; }
    bool is_number() const { return is_number_integer() || is_number_float(); }

    size_t size() const {
        if (m_type == value_t::object) return std::get<object_ptr>(m_value)->size();
        if (m_type == value_t::array) return std::get<array_ptr>(m_value)->size();
        return 0;
    }

    bool empty() const {
        if (m_type == value_t::object) return std::get<object_ptr>(m_value)->empty();
        if (m_type == value_t::array) return std::get<array_ptr>(m_value)->empty();
        return true;
    }

    bool contains(const std::string& key) const {
        if (m_type != value_t::object) return false;
        return std::get<object_ptr>(m_value)->contains(key);
    }

    iterator begin() {
        if (m_type != value_t::array) { m_type = value_t::array; m_value = std::make_shared<array_t>(); }
        return std::get<array_ptr>(m_value)->begin();
    }

    iterator end() {
        if (m_type != value_t::array) { m_type = value_t::array; m_value = std::make_shared<array_t>(); }
        return std::get<array_ptr>(m_value)->end();
    }

    const_iterator begin() const {
        if (m_type != value_t::array) throw std::runtime_error("not an array");
        return std::get<array_ptr>(m_value)->begin();
    }

    const_iterator end() const {
        if (m_type != value_t::array) throw std::runtime_error("not an array");
        return std::get<array_ptr>(m_value)->end();
    }

    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    template<typename T> T get() const;

    std::string dump(int indent = -1) const {
        std::ostringstream oss;
        dump(oss, indent, 0);
        return oss.str();
    }

    void dump(std::ostream& os, int indent, int depth) const;

    static json parse(const std::string& s) {
        size_t pos = 0;
        return parse_value(s, pos);
    }

    static json array() { return json(array_t()); }
    static json object() { return json(object_t()); }

private:
    static json parse_value(const std::string& s, size_t& pos);
    static json parse_object(const std::string& s, size_t& pos);
    static json parse_array(const std::string& s, size_t& pos);
    static std::string parse_string(const std::string& s, size_t& pos);
    static bool parse_boolean(const std::string& s, size_t& pos, bool& value);
    static bool parse_null(const std::string& s, size_t& pos);
    static bool parse_number(const std::string& s, size_t& pos, json& value);
    static void skip_ws(const std::string& s, size_t& pos);
};

template<> inline bool json::get<bool>() const {
    if (m_type != value_t::boolean) throw std::bad_variant_access();
    return std::get<bool>(m_value);
}

template<> inline long long json::get<long long>() const {
    if (m_type != value_t::number_integer) throw std::bad_variant_access();
    return std::get<int64_t>(m_value);
}

template<> inline int json::get<int>() const { return static_cast<int>(get<long long>()); }

template<> inline double json::get<double>() const {
    if (m_type == value_t::number_integer) return static_cast<double>(std::get<int64_t>(m_value));
    if (m_type == value_t::number_float) return std::get<double>(m_value);
    throw std::bad_variant_access();
}

template<> inline float json::get<float>() const { return static_cast<float>(get<double>()); }

template<> inline std::string json::get<std::string>() const {
    if (m_type != value_t::string) throw std::bad_variant_access();
    return std::get<std::string>(m_value);
}

template<> inline json::object_t json::get<json::object_t>() const {
    if (m_type != value_t::object) throw std::bad_variant_access();
    return *std::get<object_ptr>(m_value);
}

template<> inline json::array_t json::get<json::array_t>() const {
    if (m_type != value_t::array) throw std::bad_variant_access();
    return *std::get<array_ptr>(m_value);
}

template<> inline size_t json::get<size_t>() const {
    if (m_type != value_t::number_integer) throw std::bad_variant_access();
    return static_cast<size_t>(std::get<int64_t>(m_value));
}

inline void json::dump(std::ostream& os, int indent, int depth) const {
    switch (m_type) {
        case value_t::null: os << "null"; break;
        case value_t::boolean: os << (std::get<bool>(m_value) ? "true" : "false"); break;
        case value_t::number_integer: os << std::get<int64_t>(m_value); break;
        case value_t::number_float: os << std::get<double>(m_value); break;
        case value_t::string: {
            os << "\"";
            for (char c : std::get<std::string>(m_value)) {
                switch (c) {
                    case '"': os << "\\\""; break;
                    case '\\': os << "\\\\"; break;
                    case '\n': os << "\\n"; break;
                    case '\r': os << "\\r"; break;
                    case '\t': os << "\\t"; break;
                    default: os << c; break;
                }
            }
            os << "\"";
            break;
        }
        case value_t::array: {
            os << "[";
            const auto& arr = *std::get<array_ptr>(m_value);
            if (!arr.empty()) {
                if (indent >= 0) {
                    os << "\n";
                    for (size_t i = 0; i < arr.size(); ++i) {
                        os << std::string((depth + 1) * indent, ' ');
                        arr[i].dump(os, indent, depth + 1);
                        if (i != arr.size() - 1) os << ",";
                        os << "\n";
                    }
                    os << std::string(depth * indent, ' ');
                } else {
                    for (size_t i = 0; i < arr.size(); ++i) {
                        arr[i].dump(os, indent, depth);
                        if (i != arr.size() - 1) os << ",";
                    }
                }
            }
            os << "]";
            break;
        }
        case value_t::object: {
            os << "{";
            const auto& obj = *std::get<object_ptr>(m_value);
            if (!obj.empty()) {
                if (indent >= 0) {
                    os << "\n";
                    size_t i = 0;
                    for (const auto& p : obj) {
                        os << std::string((depth + 1) * indent, ' ');
                        os << "\"" << p.first << "\": ";
                        p.second.dump(os, indent, depth + 1);
                        if (i != obj.size() - 1) os << ",";
                        os << "\n";
                        ++i;
                    }
                    os << std::string(depth * indent, ' ');
                } else {
                    size_t i = 0;
                    for (const auto& p : obj) {
                        os << "\"" << p.first << "\":";
                        p.second.dump(os, indent, depth);
                        if (i != obj.size() - 1) os << ",";
                        ++i;
                    }
                }
            }
            os << "}";
            break;
        }
    }
}

inline json json::parse_value(const std::string& s, size_t& pos) {
    skip_ws(s, pos);
    if (pos >= s.size()) return nullptr;

    switch (s[pos]) {
        case '{': return parse_object(s, pos);
        case '[': return parse_array(s, pos);
        case '"': return parse_string(s, pos);
        case 't': { bool b; if (parse_boolean(s, pos, b)) return b; break; }
        case 'f': { bool b; if (parse_boolean(s, pos, b)) return b; break; }
        case 'n': { if (parse_null(s, pos)) return nullptr; break; }
        case '-': case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': {
            json value;
            if (parse_number(s, pos, value)) return value;
            break;
        }
    }
    throw std::runtime_error("invalid JSON");
}

inline json json::parse_object(const std::string& s, size_t& pos) {
    skip_ws(s, pos);
    if (s[pos] != '{') throw std::runtime_error("expected '{'");
    pos++;

    object_t obj;
    skip_ws(s, pos);

    if (s[pos] == '}') { pos++; return obj; }

    while (true) {
        skip_ws(s, pos);
        if (s[pos] != '"') throw std::runtime_error("expected string key");
        std::string key = parse_string(s, pos);
        
        skip_ws(s, pos);
        if (s[pos] != ':') throw std::runtime_error("expected ':'");
        pos++;
        
        json value = parse_value(s, pos);
        obj[key] = std::move(value);
        
        skip_ws(s, pos);
        if (s[pos] == ',') { pos++; }
        else if (s[pos] == '}') { pos++; break; }
        else { throw std::runtime_error("expected ',' or '}'"); }
    }

    return obj;
}

inline json json::parse_array(const std::string& s, size_t& pos) {
    skip_ws(s, pos);
    if (s[pos] != '[') throw std::runtime_error("expected '['");
    pos++;

    array_t arr;
    skip_ws(s, pos);

    if (s[pos] == ']') { pos++; return arr; }

    while (true) {
        json value = parse_value(s, pos);
        arr.push_back(std::move(value));
        
        skip_ws(s, pos);
        if (s[pos] == ',') { pos++; }
        else if (s[pos] == ']') { pos++; break; }
        else { throw std::runtime_error("expected ',' or ']'"); }
    }

    return arr;
}

inline std::string json::parse_string(const std::string& s, size_t& pos) {
    skip_ws(s, pos);
    if (s[pos] != '"') throw std::runtime_error("expected '\"'");
    pos++;

    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\') {
            pos++;
            if (pos >= s.size()) throw std::runtime_error("invalid escape sequence");
            switch (s[pos]) {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                default: result += s[pos]; break;
            }
        } else {
            result += s[pos];
        }
        pos++;
    }
    if (pos >= s.size()) throw std::runtime_error("unclosed string");
    pos++;
    return result;
}

inline bool json::parse_boolean(const std::string& s, size_t& pos, bool& value) {
    if (s.substr(pos, 4) == "true") { value = true; pos += 4; return true; }
    if (s.substr(pos, 5) == "false") { value = false; pos += 5; return true; }
    return false;
}

inline bool json::parse_null(const std::string& s, size_t& pos) {
    if (s.substr(pos, 4) == "null") { pos += 4; return true; }
    return false;
}

inline bool json::parse_number(const std::string& s, size_t& pos, json& result) {
    size_t start = pos;
    if (s[pos] == '-') pos++;
    if (pos >= s.size() || !isdigit(s[pos])) return false;
    
    while (pos < s.size() && isdigit(s[pos])) pos++;
    
    bool is_float = false;
    if (pos < s.size() && s[pos] == '.') {
        is_float = true;
        pos++;
        if (pos >= s.size() || !isdigit(s[pos])) return false;
        while (pos < s.size() && isdigit(s[pos])) pos++;
    }
    
    if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
        is_float = true;
        pos++;
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) pos++;
        if (pos >= s.size() || !isdigit(s[pos])) return false;
        while (pos < s.size() && isdigit(s[pos])) pos++;
    }
    
    std::string num_str = s.substr(start, pos - start);
    if (is_float) result = std::stod(num_str);
    else result = std::stoll(num_str);
    return true;
}

inline void json::skip_ws(const std::string& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t')) {
        pos++;
    }
}

inline std::ostream& operator<<(std::ostream& os, const json& j) {
    j.dump(os, -1, 0);
    return os;
}

inline std::istream& operator>>(std::istream& is, json& j) {
    std::string s((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    j = json::parse(s);
    return is;
}

}
