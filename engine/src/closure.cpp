#include "closure.hpp"
#include "object.hpp"
#include <sstream>

namespace bvm {

Value::Value(std::shared_ptr<ObjFunction> fn)
    : type(ValueType::FUNCTION), as(std::move(fn)) {}

Value::Value(std::shared_ptr<ObjClosure> cl)
    : type(ValueType::CLOSURE), as(std::move(cl)) {}

Value::Value(std::shared_ptr<ObjUpvalue> up)
    : type(ValueType::UPVALUE), as(std::move(up)) {}

std::shared_ptr<ObjFunction> Value::as_function() const {
    if (!is_function()) throw std::runtime_error("Type error: expected function");
    return std::get<std::shared_ptr<ObjFunction>>(as);
}

std::shared_ptr<ObjClosure> Value::as_closure() const {
    if (!is_closure()) throw std::runtime_error("Type error: expected closure");
    return std::get<std::shared_ptr<ObjClosure>>(as);
}

std::shared_ptr<ObjUpvalue> Value::as_upvalue() const {
    if (!is_upvalue()) throw std::runtime_error("Type error: expected upvalue");
    return std::get<std::shared_ptr<ObjUpvalue>>(as);
}

std::string Value::to_string() const {
    switch (type) {
        case ValueType::NIL: return "nil";
        case ValueType::BOOL: return std::get<bool>(as) ? "true" : "false";
        case ValueType::INT: return std::to_string(std::get<int64_t>(as));
        case ValueType::FLOAT: {
            std::ostringstream ss;
            ss << std::get<double>(as);
            return ss.str();
        }
        case ValueType::STRING: return "\"" + std::get<std::string>(as) + "\"";
        case ValueType::FUNCTION: {
            auto fn = std::get<std::shared_ptr<ObjFunction>>(as);
            return "<fn " + (fn ? fn->name : "anonymous") + ">";
        }
        case ValueType::CLOSURE: {
            auto cl = std::get<std::shared_ptr<ObjClosure>>(as);
            return "<closure " + ((cl && cl->function) ? cl->function->name : "anonymous") + ">";
        }
        case ValueType::UPVALUE: {
            auto up = std::get<std::shared_ptr<ObjUpvalue>>(as);
            return "<upvalue " + (up && up->location ? up->location->to_string() : "null") + ">";
        }
        case ValueType::OBJECT: {
            ObjHeader* obj = std::get<ObjHeader*>(as);
            if (!obj) return "<null object>";
            switch (obj->obj_type()) {
                case ObjType::STRING: {
                    auto* s = reinterpret_cast<ObjString*>(obj);
                    return "\"" + std::string(s->view()) + "\"";
                }
                case ObjType::ARRAY: {
                    auto* a = reinterpret_cast<ObjArray*>(obj);
                    return "<array [" + std::to_string(a->elements.size()) + " items]>";
                }
                case ObjType::INSTANCE: {
                    auto* inst = reinterpret_cast<ObjInstance*>(obj);
                    return "<instance " + inst->class_name + ">";
                }
                case ObjType::NATIVE_HANDLE: {
                    auto* h = reinterpret_cast<ObjNativeHandle*>(obj);
                    return "<native_handle " + h->type_name + ">";
                }
                case ObjType::WEAK_REF: {
                    auto* w = reinterpret_cast<ObjWeakRef*>(obj);
                    return "<weak_ref " + std::string(w->referent ? "<referent>" : "null") + ">";
                }
            }
            return "<object>";
        }
    }
    return "<unknown>";
}

bool Value::operator==(const Value& other) const {
    if (type != other.type) {
        // Allow numeric equality between int and float
        if (is_number() && other.is_number()) {
            return as_number() == other.as_number();
        }
        return false;
    }
    switch (type) {
        case ValueType::NIL: return true;
        case ValueType::BOOL: return std::get<bool>(as) == std::get<bool>(other.as);
        case ValueType::INT: return std::get<int64_t>(as) == std::get<int64_t>(other.as);
        case ValueType::FLOAT: return std::get<double>(as) == std::get<double>(other.as);
        case ValueType::STRING: return std::get<std::string>(as) == std::get<std::string>(other.as);
        case ValueType::FUNCTION:
            return std::get<std::shared_ptr<ObjFunction>>(as) == std::get<std::shared_ptr<ObjFunction>>(other.as);
        case ValueType::CLOSURE:
            return std::get<std::shared_ptr<ObjClosure>>(as) == std::get<std::shared_ptr<ObjClosure>>(other.as);
        case ValueType::UPVALUE:
            return std::get<std::shared_ptr<ObjUpvalue>>(as) == std::get<std::shared_ptr<ObjUpvalue>>(other.as);
        case ValueType::OBJECT: {
            ObjHeader* a = std::get<ObjHeader*>(as);
            ObjHeader* b = std::get<ObjHeader*>(other.as);
            if (a == b) return true;
            if (!a || !b) return false;
            if (a->obj_type() == ObjType::STRING && b->obj_type() == ObjType::STRING) {
                auto* sa = reinterpret_cast<ObjString*>(a);
                auto* sb = reinterpret_cast<ObjString*>(b);
                return sa->hash == sb->hash && sa->view() == sb->view();
            }
            return a == b;
        }
    }
    return false;
}

} // namespace bvm
