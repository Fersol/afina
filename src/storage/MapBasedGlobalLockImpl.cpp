#include "MapBasedGlobalLockImpl.h"

#include <mutex>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);

    std::string _tmp;
    bool is_old = false;
    auto _elem_size = key.size() + value.size();
    
    auto it = _backend.find(key);
    if (is_old = it != _backend.end()) {
        _list->move_to_front(_backend[key]); 
        _tmp = _list->front()->value;
    }

    if (_elem_size > _max_size){
    	return false;
    }
    
    if (is_old) {
    	_elem_size = value.size() - _tmp.size();
    }

    while (_size + _elem_size > _max_size) {
        auto last = _list->back();
        _size -= (last->key.size() + last->key.size());
        _backend.erase(_list->back()->key);
        _list->pop_back();
    }
    
    if (is_old) {
        _backend.at(key)->value = value;
    }
    else {
    	_list->push_front(key, value);
    	_backend.emplace(_list->front()->key, _list->front());
    }

    _size += _elem_size;
    
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);
    
    auto _elem_size = key.size() + value.size();
    auto it = _backend.find(key); 
    if (it != _backend.end()) {
        _list->move_to_front(it->second); 
        return false;
    }

    if (_elem_size > _max_size){
    	return false;
    }
    

    while (_size + _elem_size > _max_size) {
        auto last = _list->back();
        _size -= (last->key.size() + last->key.size());
        _backend.erase(_list->back()->key);
        _list->pop_back();
    }
    

    _list->push_front(key, value);
    _backend.emplace(_list->front()->key, _list->front());
    _size += _elem_size;
    
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value) {
    std::unique_lock<std::mutex> guard(_lock);

    std::string _tmp;
    auto _elem_size = key.size() + value.size();

    if (_backend.find(key) != _backend.end()) {
        _list->move_to_front(_backend[key]); 
        _tmp = _list->front()->value;
    }
    else {
        return false;
    }

    if (_elem_size > _max_size){
    	return false;
    }
    
    _elem_size = value.size() - _tmp.size();

    while (_size + _elem_size > _max_size) {
        auto last = _list->back();
        _size -= (last->key.size() + last->key.size());
        _backend.erase(_list->back()->key);
        _list->pop_back();
    }
    
    _backend[key]->value = value;
    _size += _elem_size;
    
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key) {
    std::unique_lock<std::mutex> guard(_lock);
    std::string _tmp;

    if (_backend.find(key) != _backend.end()) {
        _list->move_to_front(_backend[key]); 
        _tmp = _list->front()->value;
        _list->del_front();
        _backend.erase(key);
        _size -= key.size() + _tmp.size();
    }

    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const {
    std::unique_lock<std::mutex> guard(_lock);
    
    auto it = _backend.find(key);
    if (it != _backend.end()) {
        _list->move_to_front(it->second);
        value = _list->front()->value;
        return true;
    }
    return false;
}


Dlink_list::Dlink_list() { head = NULL; }

Dlink_list::~Dlink_list() {
    while (head) {
        Node *tmp = head;
        head = head->next;
        delete (tmp);
    }
}

void Dlink_list::push_front(std::string key, std::string value) {

    Node *node = new Node();
    node->key = key;
    node->value = value;
    node->prev = NULL;

    if (head == NULL) {
        node->next = NULL;
        head = node;
        tail = node;
    } else {
        node->next = head;
        node->next->prev = node;
        head = node;
    }
}

void Dlink_list::pop_back() {
    Node *tmp = tail;
    tail->prev->next = NULL;
    tail = tail->prev;
    delete (tmp);
}

void Dlink_list::del_front() {
    Node *tmp = head;
    head = head->next;
    head->prev = NULL;
    delete (tmp);
}

void Dlink_list::move_to_front(Node *node) {
    if (node != head) {
        if (node == tail) {
            tail = node->prev;
        } else {
            node->next->prev = node->prev;
        }
        node->prev->next = node->next;
        node->prev = NULL;
        head->prev = node;
        node->next = head;
        head = node;
    }
}

Node *Dlink_list::front() { return head; }

Node *Dlink_list::back() { return tail; }


} // namespace Backend
} // namespace Afina