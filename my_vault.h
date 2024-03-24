#include <fmt/format.h>
#include <fmt/ostream.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <atomic>

#define LOCK_FREE 1

using namespace std::chrono_literals;

template<class ElementData, size_t COUNT = 1024>
class Vault
{
    struct Element {
        ElementData data;
        std::mutex  access;
#if LOCK_FREE
        std::atomic_bool inUse {false};
#else
        bool inUse {false};
#endif
    };

    std::array<Element, COUNT> storage;
#if !LOCK_FREE
    std::mutex access;
#endif
public:
    struct iterator;

    class ElementView
    {
        std::unique_lock<std::mutex> lock;
        Element*                     ref {nullptr};

        ElementView( ) = default;

        explicit ElementView(Element& e) : lock {e.access}, ref {&e} { }
        friend class Vault;

    public:
        ElementData& operator( ) ( )
        {
            if ( !ref->inUse )
                throw std::out_of_range {"no such data"};
            return ref->data;
        }

        const ElementData& operator( ) ( ) const
        {
            if ( !ref->inUse )
                throw std::out_of_range {"no such data const"};
            return ref->data;
        }

        operator bool ( ) const { return ref && ref->inUse; }
    };

    ElementView view (size_t idx) { return ElementView {storage.at(idx)}; }

    std::pair<ElementView, bool> allocate ( )
    {
        iterator i {*this};
#if LOCK_FREE
        do {
            i.iter = std::ranges::find_if_not(storage, &Element::inUse);
            if ( i.iter == storage.end( ) ) {
                // throw std::out_of_range {"no empty element found"};
                return std::make_pair(ElementView { }, false);
            }
            bool        exp {false};
            ElementView v {*i.iter};
            if ( i.iter->inUse.compare_exchange_strong(exp, true) )
                return {std::move(v), true};
        } while ( true );
#else
        std::unique_lock _ {access};
        i.iter = std::ranges::find_if_not(storage, &Element::inUse);
        if ( i.iter == storage.end( ) ) {
            // throw std::out_of_range {"no empty element found"};
            return {ElementView { }, false};
        }
        ElementView v {*i.iter};
        i.iter->inUse = true;
        return {std::move(v), true};
#endif
    }

    bool deallocate (size_t idx)
    {
#if LOCK_FREE
        ElementView e {storage.at(idx)};
        bool        exp {true};
        return e.ref->inUse.compare_exchange_strong(exp, false);
#else
        std::unique_lock _1 {access};
        ElementView      e {storage.at(idx)};
        return std::exchange(e.ref->inUse, false);
#endif
    }

    bool deallocate (const std::function<bool(const ElementData&)>& pred)
    {
#if LOCK_FREE
        for ( auto& e: storage ) {
            ElementView v {e};
            if ( v && pred(v( )) ) {
                bool exp {true};
                if ( e.inUse.compare_exchange_weak(exp, false) )
                    return true;
            }
        }
        return false;
#else
        std::unique_lock _1 {access};
        do {
            auto iter = std::ranges::find_if(storage, [pred] (const Element& e) { return e.inUse && pred(e.data); });
            if ( iter == storage.cend( ) ) {
                // throw std::out_of_range{"no such element"};
                return false;
            }
            std::unique_lock _2 {iter->access};
            if ( !pred(iter->data) )
                continue;
            return std::exchange(iter->inUse, false);
        } while ( true );
#endif
    }

    void dump ( ) const
    {
        for ( size_t i = 0; i < COUNT; i++ ) {
            const auto& e = storage.at(i);
            if ( e.inUse )
                fmt::print("{} {}\n", i, fmt::streamed(e.data));
        }
    }

    struct iterator {
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = ElementView;
        using pointer           = value_type*;        // or also value_type*
        using const_pointer     = const value_type*;  // or also value_type*
        using reference         = value_type&;        // or also value_type&
        using const_reference   = const value_type&;  // or also value_type&

        iterator& operator++ ( )
        {
#if LOCK_FREE
            iter = std::find_if(iter + 1, owner.storage.end( ), [] (const Element& e) { return e.inUse.load( ); });
#else
            iter = std::find_if(iter + 1, owner.storage.end( ), [] (const Element& e) { return e.inUse; });
#endif
            return *this;
        }

        value_type operator* ( ) { return ElementView {*iter}; }

        bool operator== (const iterator& o) const { return iter == o.iter; }

    private:
        explicit iterator(Vault& v) : owner {v} { }

        Vault&                               owner;
        std::array<Element, COUNT>::iterator iter;
        friend class Vault;
    };

    iterator begin ( )
    {
        iterator i {*this};
#if LOCK_FREE
        i.iter = std::ranges::find_if(storage, [] (const Element& e) { return e.inUse.load( ); });
#else
        i.iter = std::ranges::find_if(storage, [] (const Element& e) { return e.inUse; });
#endif
        return i;
    }

    iterator end ( )
    {
        iterator i {*this};
        //        i.iter = std::ranges::find_if(storage|std::views::reverse, [](const Element& e){return  e.inUse.load();}).base();
        i.iter = storage.end( );
        return i;
    }

    [[nodiscard]] size_t capacity ( ) const { return COUNT; }
};
