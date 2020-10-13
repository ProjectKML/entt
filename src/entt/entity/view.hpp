#ifndef ENTT_ENTITY_VIEW_HPP
#define ENTT_ENTITY_VIEW_HPP


#include <iterator>
#include <array>
#include <tuple>
#include <utility>
#include <algorithm>
#include <type_traits>
#include "../config/config.h"
#include "../core/type_traits.hpp"
#include "entity.hpp"
#include "fwd.hpp"
#include "pool.hpp"
#include "sparse_set.hpp"
#include "utility.hpp"


namespace entt {


/**
 * @brief View.
 *
 * Primary template isn't defined on purpose. All the specializations give a
 * compile-time error, but for a few reasonable cases.
 */
template<typename...>
class basic_view;


/**
 * @brief Multi component view.
 *
 * Multi component views iterate over those entities that have at least all the
 * given components in their bags. During initialization, a multi component view
 * looks at the number of entities available for each component and uses the
 * smallest set in order to get a performance boost when iterate.
 *
 * @b Important
 *
 * Iterators aren't invalidated if:
 *
 * * New instances of the given components are created and assigned to entities.
 * * The entity currently pointed is modified (as an example, if one of the
 *   given components is removed from the entity to which the iterator points).
 * * The entity currently pointed is destroyed.
 *
 * In all other cases, modifying the pools iterated by the view in any way
 * invalidates all the iterators and using them results in undefined behavior.
 *
 * @note
 * Views share references to the underlying data structures of the registry that
 * generated them. Therefore any change to the entities and to the components
 * made by means of the registry are immediately reflected by views.
 *
 * @warning
 * Lifetime of a view must not overcome that of the registry that generated it.
 * In any other case, attempting to use a view results in undefined behavior.
 *
 * @tparam Entity A valid entity type (see entt_traits for more details).
 * @tparam Exclude Types of components used to filter the view.
 * @tparam Component Types of components iterated by the view.
 */
template<typename Entity, typename... Exclude, typename... Component>
class basic_view<Entity, exclude_t<Exclude...>, Component...>
    : public basic_view<Entity, exclude_t<>, std::add_const_t<Exclude>>...,
      public basic_view<Entity, exclude_t<>, Component>...
{
    template<typename Comp>
    using view_type = basic_view<Entity, exclude_t<>, Comp>;

    using return_type = type_list_cat_t<std::conditional_t<is_eto_eligible_v<Component>, type_list<>, type_list<Component>>...>;
    using leading_type = std::tuple_element_t<0, std::tuple<Component...>>;

    class view_iterator final {
        friend class basic_view<Entity, exclude_t<Exclude...>, Component...>;

        using iterator = typename view_type<leading_type>::iterator;

        view_iterator(iterator curr, const basic_view &ref) ENTT_NOEXCEPT
            : first{ref.view_type<leading_type>::begin()},
              last{ref.view_type<leading_type>::end()},
              it{curr},
              parent{&ref}
        {
            if(it != last && !parent->valid(*it)) {
                ++(*this);
            }
        }

    public:
        using difference_type = typename std::iterator_traits<iterator>::difference_type;
        using value_type = typename std::iterator_traits<iterator>::value_type;
        using pointer = typename std::iterator_traits<iterator>::pointer;
        using reference = typename std::iterator_traits<iterator>::reference;
        using iterator_category = std::bidirectional_iterator_tag;

        view_iterator() ENTT_NOEXCEPT = default;

        view_iterator & operator++() {
            while(++it != last && !parent->valid(*it));
            return *this;
        }

        view_iterator operator++(int) {
            view_iterator orig = *this;
            return ++(*this), orig;
        }

        view_iterator & operator--() ENTT_NOEXCEPT {
            while(--it != first && !parent->valid(*it));
            return *this;
        }

        view_iterator operator--(int) ENTT_NOEXCEPT {
            view_iterator orig = *this;
            return operator--(), orig;
        }

        [[nodiscard]] bool operator==(const view_iterator &other) const ENTT_NOEXCEPT {
            return other.it == it;
        }

        [[nodiscard]] bool operator!=(const view_iterator &other) const ENTT_NOEXCEPT {
            return !(*this == other);
        }

        [[nodiscard]] pointer operator->() const {
            return &*it;
        }

        [[nodiscard]] reference operator*() const {
            return *operator->();
        }

    private:
        iterator first;
        iterator last;
        iterator it;
        const basic_view *parent;
    };

    class view_range {
        friend class basic_view<Entity, exclude_t<Exclude...>, Component...>;

        class range_iterator {
            friend class view_range;

            using iterator = typename std::decay_t<decltype(std::declval<view_type<leading_type>>().all())>::iterator;

            template<typename Comp>
            [[nodiscard]] auto get(const view_type<Comp> &curr) const {
                if constexpr(std::is_same_v<Comp, leading_type>) {
                    return *it;
                } else if constexpr(is_eto_eligible_v<Comp>) {
                    return std::make_tuple();
                } else {
                    return std::forward_as_tuple(curr.get(std::get<0>(*it)));
                }
            }

            range_iterator(iterator curr, const basic_view &ref) ENTT_NOEXCEPT
                : last{ref.view_type<leading_type>::all().end()},
                  it{curr},
                  parent{&ref}
            {
                if(it != last && !parent->valid(std::get<0>(*it))) {
                    ++(*this);
                }
            }

        public:
            using difference_type = std::ptrdiff_t;
            using value_type = decltype(std::tuple_cat(
                std::declval<std::tuple<Entity>>(),
                std::declval<std::conditional_t<is_eto_eligible_v<Component>, std::tuple<>, std::tuple<Component &>>>()...
            ));
            using pointer = void;
            using reference = value_type;
            using iterator_category = std::input_iterator_tag;

            range_iterator & operator++() ENTT_NOEXCEPT {
                while(++it != last && !parent->valid(std::get<0>(*it)));
                return *this;
            }

            range_iterator operator++(int) ENTT_NOEXCEPT {
                range_iterator orig = *this;
                return ++(*this), orig;
            }

            [[nodiscard]] reference operator*() const ENTT_NOEXCEPT {
                return std::tuple_cat(get<Component>(*parent)...);
            }

            [[nodiscard]] bool operator==(const range_iterator &other) const ENTT_NOEXCEPT {
                return other.it == it;
            }

            [[nodiscard]] bool operator!=(const range_iterator &other) const ENTT_NOEXCEPT {
                return !(*this == other);
            }

        private:
            iterator last;
            iterator it;
            const basic_view *parent;
        };

        view_range(const basic_view &ref)
            : parent{&ref}
        {}

    public:
        using iterator = range_iterator;

        [[nodiscard]] iterator begin() const ENTT_NOEXCEPT {
            return { parent->view_type<leading_type>::all().begin(), *parent };
        }

        [[nodiscard]] iterator end() const ENTT_NOEXCEPT {
            return { parent->view_type<leading_type>::all().end(), *parent };
        }

    private:
        const basic_view *parent;
    };

    [[nodiscard]] bool valid(const Entity entt) const {
        return ((std::is_same_v<Component, leading_type> || view_type<Component>::contains(entt)) && ...)
            && !(view_type<std::add_const_t<Exclude>>::contains(entt) || ...);
    }

    template<typename Other, typename Comp>
    [[nodiscard]] decltype(auto) get([[maybe_unused]] Comp &comp, [[maybe_unused]] const view_type<Other> &other, [[maybe_unused]] const Entity entt) const {
        if constexpr(std::is_same_v<Comp, Other>) {
            return comp;
        } else {
            return other.get(entt);
        }
    }

    template<typename Comp, typename Func, typename... Type>
    void traverse(Func func, type_list<Type...>) const {
        if constexpr(std::disjunction_v<std::is_same<Comp, Type>...>) {
            for(auto [entt, comp]: view_type<Comp>::all()) {
                if(((std::is_same_v<Comp, Component> || view_type<Component>::contains(entt)) && ...) && !(view_type<std::add_const_t<Exclude>>::contains(entt) || ...)) {
                    if constexpr(std::is_invocable_v<Func, decltype(get<Type>({}))...>) {
                        func(get<Type>(comp, *this, entt)...);
                    } else {
                        func(entt, get<Type>(comp, *this, entt)...);
                    }
                }
            }
        } else {
            for(auto entt: static_cast<const view_type<Comp> &>(*this)) {
                if(((std::is_same_v<Comp, Component> || view_type<Component>::contains(entt)) && ...) && !(view_type<std::add_const_t<Exclude>>::contains(entt) || ...)) {
                    if constexpr(std::is_invocable_v<Func, decltype(get<Type>({}))...>) {
                        func(view_type<Type>::get(entt)...);
                    } else {
                        func(entt, view_type<Type>::get(entt)...);
                    }
                }
            }
        }
    }

public:
    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;
    /*! @brief Unsigned integer type. */
    using size_type = std::size_t;
    /*! @brief Bidirectional iterator type. */
    using iterator = view_iterator;
    /*! @brief Reverse iterator type. */
    using reverse_iterator = std::reverse_iterator<view_iterator>;

    /**
     * @brief Constructs a multi component view.
     * @param component Views of the components to iterate.
     * @param exclude Views of the components to use as filters.
     */
    basic_view(basic_view<Entity, exclude_t<>, Component>... component, basic_view<Entity, exclude_t<>, std::add_const_t<Exclude>> ... exclude) ENTT_NOEXCEPT
        : basic_view<Entity, exclude_t<>, std::add_const_t<Exclude>>{std::move(exclude)}...,
          basic_view<Entity, exclude_t<>, Component>{std::move(component)}...
    {}

    /**
     * @brief Estimates the number of entities iterated by the view.
     * @return Estimated number of entities iterated by the view.
     */
    [[nodiscard]] size_type size_hint() const ENTT_NOEXCEPT {
        return (std::min)({ view_type<Component>::size()... });
    }

    /**
     * @brief Returns an iterator to the first entity of the view.
     *
     * The returned iterator points to the first entity of the view. If the view
     * is empty, the returned iterator will be equal to `end()`.
     *
     * @note
     * Iterators stay true to the order imposed to the underlying data
     * structures.
     *
     * @return An iterator to the first entity of the view.
     */
    [[nodiscard]] iterator begin() const {
        return iterator{view_type<leading_type>::begin(), *this};
    }

    /**
     * @brief Returns an iterator that is past the last entity of the view.
     *
     * The returned iterator points to the entity following the last entity of
     * the view. Attempting to dereference the returned iterator results in
     * undefined behavior.
     *
     * @note
     * Iterators stay true to the order imposed to the underlying data
     * structures.
     *
     * @return An iterator to the entity following the last entity of the view.
     */
    [[nodiscard]] iterator end() const {
        return iterator{view_type<leading_type>::end(), *this};
    }

    /**
     * @brief Returns an iterator to the first entity of the reversed view.
     *
     * The returned iterator points to the first entity of the reversed view. If
     * the view is empty, the returned iterator will be equal to `rend()`.
     *
     * @note
     * Iterators stay true to the order imposed to the underlying data
     * structures.
     *
     * @return An iterator to the first entity of the reversed view.
     */
    [[nodiscard]] reverse_iterator rbegin() const {
        return std::make_reverse_iterator(end());
    }

    /**
     * @brief Returns an iterator that is past the last entity of the reversed
     * view.
     *
     * The returned iterator points to the entity following the last entity of
     * the reversed view. Attempting to dereference the returned iterator
     * results in undefined behavior.
     *
     * @note
     * Iterators stay true to the order imposed to the underlying data
     * structures.
     *
     * @return An iterator to the entity following the last entity of the
     * reversed view.
     */
    [[nodiscard]] reverse_iterator rend() const {
        return std::make_reverse_iterator(begin());
    }

    /**
     * @brief Returns the first entity of the view, if any.
     * @return The first entity of the view if one exists, the null entity
     * otherwise.
     */
    [[nodiscard]] entity_type front() const {
        const auto it = begin();
        return it != end() ? *it : null;
    }

    /**
     * @brief Returns the last entity of the view, if any.
     * @return The last entity of the view if one exists, the null entity
     * otherwise.
     */
    [[nodiscard]] entity_type back() const {
        const auto it = rbegin();
        return it != rend() ? *it : null;
    }

    /**
     * @brief Finds an entity.
     * @param entt A valid entity identifier.
     * @return An iterator to the given entity if it's found, past the end
     * iterator otherwise.
     */
    [[nodiscard]] iterator find(const entity_type entt) const {
        iterator it{view_type<leading_type>::find(entt), *this};
        return (it != end() && *it == entt) ? it : end();
    }

    /**
     * @brief Checks if a view contains an entity.
     * @param entt A valid entity identifier.
     * @return True if the view contains the given entity, false otherwise.
     */
    [[nodiscard]] bool contains(const entity_type entt) const {
        return (view_type<Component>::contains(entt) && ...) && !(view_type<std::add_const_t<Exclude>>::contains(entt) || ...);
    }

    /**
     * @brief Returns the components assigned to the given entity.
     *
     * Prefer this function instead of `registry::get` during iterations. It has
     * far better performance than its counterpart.
     *
     * @warning
     * Attempting to use an invalid component type results in a compilation
     * error. Attempting to use an entity that doesn't belong to the view
     * results in undefined behavior.<br/>
     * An assertion will abort the execution at runtime in debug mode if the
     * view doesn't contain the given entity.
     *
     * @tparam Comp Types of components to get.
     * @param entt A valid entity identifier.
     * @return The components assigned to the entity.
     */
    template<typename... Comp>
    [[nodiscard]] decltype(auto) get([[maybe_unused]] const entity_type entt) const {
        ENTT_ASSERT(contains(entt));

        if constexpr(sizeof...(Comp) == 1) {
            return (view_type<Comp>::get(entt), ...);
        } else {
            return std::forward_as_tuple(get<Comp>(entt)...);
        }
    }

    /**
     * @brief Iterates entities and components and applies the given function
     * object to them.
     *
     * The function object is invoked for each entity. It is provided with the
     * entity itself and a set of references to non-empty components. The
     * _constness_ of the components is as requested.<br/>
     * The signature of the function must be equivalent to one of the following
     * forms:
     *
     * @code{.cpp}
     * void(const entity_type, Type &...);
     * void(Type &...);
     * @endcode
     *
     * @note
     * Empty types aren't explicitly instantiated and therefore they are never
     * returned during iterations.
     *
     * @tparam Func Type of the function object to invoke.
     * @param func A valid function object.
     */
    template<typename Func>
    void each(Func func) const {
        const auto size = std::min({ view_type<Component>::size()... });
        static_cast<void>(((view_type<Component>::size() == size ? (traverse<Component>(std::move(func), return_type{}), true) : false) || ...));
    }

    /**
     * @brief Iterates entities and components and applies the given function
     * object to them.
     *
     * The pool of the suggested component is used to lead the iterations. The
     * returned entities will therefore respect the order of the pool associated
     * with that type.<br/>
     * It is no longer guaranteed that the performance is the best possible, but
     * there will be greater control over the order of iteration.
     *
     * @sa each
     *
     * @tparam Comp Type of component to use to enforce the iteration order.
     * @tparam Func Type of the function object to invoke.
     * @param func A valid function object.
     */
    template<typename Comp, typename Func>
    void each(Func func) const {
        traverse<Comp>(std::move(func), return_type{});
    }

    /**
     * @brief Returns an iterable object to use to _visit_ the view.
     *
     * The iterable object returns tuples that contain the current entity and a
     * set of references to its non-empty components. The _constness_ of the
     * components is as requested.
     *
     * @note
     * Empty types aren't explicitly instantiated and therefore they are never
     * returned during iterations.
     *
     * @return An iterable object to use to _visit_ the view.
     */
    [[nodiscard]] auto all() const & ENTT_NOEXCEPT {
        return view_range{*this};
    }

    /*! @brief Prevents subtle errors with temporaries. */
    [[nodiscard]] auto all() const && ENTT_NOEXCEPT = delete;
};


/**
 * @brief Single component view specialization.
 *
 * Single component views are specialized in order to get a boost in terms of
 * performance. This kind of views can access the underlying data structure
 * directly and avoid superfluous checks.
 *
 * @b Important
 *
 * Iterators aren't invalidated if:
 *
 * * New instances of the given component are created and assigned to entities.
 * * The entity currently pointed is modified (as an example, the given
 *   component is removed from the entity to which the iterator points).
 * * The entity currently pointed is destroyed.
 *
 * In all other cases, modifying the pool iterated by the view in any way
 * invalidates all the iterators and using them results in undefined behavior.
 *
 * @note
 * Views share a reference to the underlying data structure of the registry that
 * generated them. Therefore any change to the entities and to the components
 * made by means of the registry are immediately reflected by views.
 *
 * @warning
 * Lifetime of a view must not overcome that of the registry that generated it.
 * In any other case, attempting to use a view results in undefined behavior.
 *
 * @tparam Entity A valid entity type (see entt_traits for more details).
 * @tparam Component Type of component iterated by the view.
 */
template<typename Entity, typename Component>
class basic_view<Entity, exclude_t<>, Component> {
    /*! @brief A registry is allowed to create views. */
    friend class basic_registry<Entity>;

    using pool_type = pool_t<Entity, Component>;

    class view_range {
        friend class basic_view<Entity, exclude_t<>, Component>;

        class range_iterator {
            friend class view_range;

            using it_type = std::conditional_t<
                is_eto_eligible_v<Component>,
                std::tuple<typename basic_sparse_set<Entity>::iterator>,
                std::tuple<typename basic_sparse_set<Entity>::iterator, decltype(std::declval<pool_type>().begin())>
            >;

            range_iterator(it_type from) ENTT_NOEXCEPT
                : it{from}
            {}

        public:
            using difference_type = std::ptrdiff_t;
            using value_type = std::conditional_t<is_eto_eligible_v<Component>, std::tuple<Entity>, std::tuple<Entity, Component &>>;
            using pointer = void;
            using reference = value_type;
            using iterator_category = std::input_iterator_tag;

            range_iterator & operator++() ENTT_NOEXCEPT {
                return std::apply([](auto &&... curr) { (++curr, ...); }, it), *this;
            }

            range_iterator operator++(int) ENTT_NOEXCEPT {
                range_iterator orig = *this;
                return ++(*this), orig;
            }

            [[nodiscard]] reference operator*() const ENTT_NOEXCEPT {
                return std::apply([](auto &&... curr) { return reference{*curr...}; }, it);
            }

            [[nodiscard]] bool operator==(const range_iterator &other) const ENTT_NOEXCEPT {
                return std::get<0>(other.it) == std::get<0>(it);
            }

            [[nodiscard]] bool operator!=(const range_iterator &other) const ENTT_NOEXCEPT {
                return !(*this == other);
            }

        private:
            it_type it{};
        };

        view_range(pool_type &ref)
            : pool{&ref}
        {}

    public:
        using iterator = range_iterator;

        [[nodiscard]] iterator begin() const ENTT_NOEXCEPT {
            if constexpr(is_eto_eligible_v<Component>) {
                return range_iterator{std::make_tuple(pool->basic_sparse_set<entity_type>::begin())};
            } else {
                return range_iterator{std::make_tuple(pool->basic_sparse_set<entity_type>::begin(), pool->begin())};
            }
        }

        [[nodiscard]] iterator end() const ENTT_NOEXCEPT {
            if constexpr(is_eto_eligible_v<Component>) {
                return range_iterator{std::make_tuple(pool->basic_sparse_set<entity_type>::end())};
            } else {
                return range_iterator{std::make_tuple(pool->basic_sparse_set<entity_type>::end(), pool->end())};
            }
        }

    private:
        pool_type *pool;
    };

    basic_view(pool_type &ref) ENTT_NOEXCEPT
        : pool{&ref}
    {}

public:
    /*! @brief Type of component iterated by the view. */
    using raw_type = Component;
    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;
    /*! @brief Unsigned integer type. */
    using size_type = std::size_t;
    /*! @brief Random access iterator type. */
    using iterator = typename basic_sparse_set<Entity>::iterator;
    /*! @brief Reversed iterator type. */
    using reverse_iterator = typename basic_sparse_set<Entity>::reverse_iterator;

    /**
     * @brief Returns the number of entities that have the given component.
     * @return Number of entities that have the given component.
     */
    [[nodiscard]] size_type size() const ENTT_NOEXCEPT {
        return pool->size();
    }

    /**
     * @brief Checks whether a view is empty.
     * @return True if the view is empty, false otherwise.
     */
    [[nodiscard]] bool empty() const ENTT_NOEXCEPT {
        return pool->empty();
    }

    /**
     * @brief Direct access to the list of components.
     *
     * The returned pointer is such that range `[raw(), raw() + size())` is
     * always a valid range, even if the container is empty.
     *
     * @note
     * Components are in the reverse order as returned by the `begin`/`end`
     * iterators.
     *
     * @return A pointer to the array of components.
     */
    [[nodiscard]] raw_type * raw() const ENTT_NOEXCEPT {
        return pool->raw();
    }

    /**
     * @brief Direct access to the list of entities.
     *
     * The returned pointer is such that range `[data(), data() + size())` is
     * always a valid range, even if the container is empty.
     *
     * @note
     * Entities are in the reverse order as returned by the `begin`/`end`
     * iterators.
     *
     * @return A pointer to the array of entities.
     */
    [[nodiscard]] const entity_type * data() const ENTT_NOEXCEPT {
        return pool->data();
    }

    /**
     * @brief Returns an iterator to the first entity of the view.
     *
     * The returned iterator points to the first entity of the view. If the view
     * is empty, the returned iterator will be equal to `end()`.
     *
     * @note
     * Iterators stay true to the order imposed to the underlying data
     * structures.
     *
     * @return An iterator to the first entity of the view.
     */
    [[nodiscard]] iterator begin() const ENTT_NOEXCEPT {
        return pool->basic_sparse_set<Entity>::begin();
    }

    /**
     * @brief Returns an iterator that is past the last entity of the view.
     *
     * The returned iterator points to the entity following the last entity of
     * the view. Attempting to dereference the returned iterator results in
     * undefined behavior.
     *
     * @note
     * Iterators stay true to the order imposed to the underlying data
     * structures.
     *
     * @return An iterator to the entity following the last entity of the view.
     */
    [[nodiscard]] iterator end() const ENTT_NOEXCEPT {
        return pool->basic_sparse_set<Entity>::end();
    }

    /**
     * @brief Returns an iterator to the first entity of the reversed view.
     *
     * The returned iterator points to the first entity of the reversed view. If
     * the view is empty, the returned iterator will be equal to `rend()`.
     *
     * @note
     * Iterators stay true to the order imposed to the underlying data
     * structures.
     *
     * @return An iterator to the first entity of the reversed view.
     */
    [[nodiscard]] reverse_iterator rbegin() const ENTT_NOEXCEPT {
        return pool->basic_sparse_set<Entity>::rbegin();
    }

    /**
     * @brief Returns an iterator that is past the last entity of the reversed
     * view.
     *
     * The returned iterator points to the entity following the last entity of
     * the reversed view. Attempting to dereference the returned iterator
     * results in undefined behavior.
     *
     * @note
     * Iterators stay true to the order imposed to the underlying data
     * structures.
     *
     * @return An iterator to the entity following the last entity of the
     * reversed view.
     */
    [[nodiscard]] reverse_iterator rend() const ENTT_NOEXCEPT {
        return pool->basic_sparse_set<Entity>::rend();
    }

    /**
     * @brief Returns the first entity of the view, if any.
     * @return The first entity of the view if one exists, the null entity
     * otherwise.
     */
    [[nodiscard]] entity_type front() const {
        const auto it = begin();
        return it != end() ? *it : null;
    }

    /**
     * @brief Returns the last entity of the view, if any.
     * @return The last entity of the view if one exists, the null entity
     * otherwise.
     */
    [[nodiscard]] entity_type back() const {
        const auto it = rbegin();
        return it != rend() ? *it : null;
    }

    /**
     * @brief Finds an entity.
     * @param entt A valid entity identifier.
     * @return An iterator to the given entity if it's found, past the end
     * iterator otherwise.
     */
    [[nodiscard]] iterator find(const entity_type entt) const {
        const auto it = pool->find(entt);
        return it != end() && *it == entt ? it : end();
    }

    /**
     * @brief Returns the identifier that occupies the given position.
     * @param pos Position of the element to return.
     * @return The identifier that occupies the given position.
     */
    [[nodiscard]] entity_type operator[](const size_type pos) const {
        return begin()[pos];
    }

    /**
     * @brief Checks if a view contains an entity.
     * @param entt A valid entity identifier.
     * @return True if the view contains the given entity, false otherwise.
     */
    [[nodiscard]] bool contains(const entity_type entt) const {
        return pool->contains(entt);
    }

    /**
     * @brief Returns the component assigned to the given entity.
     *
     * Prefer this function instead of `registry::get` during iterations. It has
     * far better performance than its counterpart.
     *
     * @warning
     * Attempting to use an entity that doesn't belong to the view results in
     * undefined behavior.<br/>
     * An assertion will abort the execution at runtime in debug mode if the
     * view doesn't contain the given entity.
     *
     * @param entt A valid entity identifier.
     * @return The component assigned to the entity.
     */
    template<typename Comp = Component>
    [[nodiscard]] decltype(auto) get(const entity_type entt) const {
        static_assert(std::is_same_v<Comp, Component>, "Invalid component type");
        ENTT_ASSERT(contains(entt));
        return pool->get(entt);
    }

    /**
     * @brief Iterates entities and components and applies the given function
     * object to them.
     *
     * The function object is invoked for each entity. It is provided with the
     * entity itself and a reference to the component if it's a non-empty one.
     * The _constness_ of the component is as requested.<br/>
     * The signature of the function must be equivalent to one of the following
     * forms:
     *
     * @code{.cpp}
     * void(const entity_type, Component &);
     * void(Component &);
     * @endcode
     *
     * @note
     * Empty types aren't explicitly instantiated and therefore they are never
     * returned during iterations.
     *
     * @tparam Func Type of the function object to invoke.
     * @param func A valid function object.
     */
    template<typename Func>
    void each(Func func) const {
        if constexpr(is_eto_eligible_v<Component>) {
            if constexpr(std::is_invocable_v<Func>) {
                for(auto pos = pool->size(); pos; --pos) {
                    func();
                }
            } else {
                for(const auto entt: *this) {
                    func(entt);
                }
            }
        } else {
            if constexpr(std::is_invocable_v<Func, decltype(get({}))>) {
                for(auto &&component: *pool) {
                    func(component);
                }
            } else {
                auto raw = pool->begin();

                for(const auto entt: *this) {
                    func(entt, *(raw++));
                }
            }
        }
    }

    /**
     * @brief Returns an iterable object to use to _visit_ the view.
     *
     * The iterable object returns tuples that contain the current entity and a
     * reference to its component if it's a non-empty one. The _constness_ of
     * the component is as requested.
     *
     * @note
     * Empty types aren't explicitly instantiated and therefore they are never
     * returned during iterations.
     *
     * @return An iterable object to use to _visit_ the view.
     */
    [[nodiscard]] auto all() const ENTT_NOEXCEPT {
        return view_range{*pool};
    }

private:
    pool_type *pool;
};


}


#endif
