/*
 * S2E Selective Symbolic Execution Framework
 *
 * Copyright (c) 2014, Dependable Systems Laboratory, EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Currently maintained by:
 *    Stefan Bucur <stefan.bucur@epfl.ch>
 *
 * All contributors are listed in the S2E-AUTHORS file.
 */

#ifndef ROUNDROBIN_H_
#define ROUNDROBIN_H_

#include <map>
#include <set>
#include <vector>
#include <list>
#include <iostream>

#include <assert.h>
#include <stdlib.h>

#include <klee/Internal/ADT/DiscretePDF.h>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/utility.hpp>

namespace s2e {


using boost::shared_ptr;
using boost::scoped_ptr;


struct RandStdlib {
	int operator()() {
		return rand();
	}
};


template<class Value>
class Selector {
public:
	typedef Value value_type;

	virtual ~Selector() {}

	virtual value_type select() = 0;
	virtual bool insert(const value_type &value) = 0;
	virtual bool erase(const value_type &value) = 0;
	virtual void clear() = 0;
	virtual bool empty() const = 0;
	virtual size_t size() const = 0;

	virtual void updateWeights() {}

	// XXX: This is a hack to avoid implementing an iterator over the elements
	virtual void copyTo(std::vector<value_type> &dst) = 0;
};


template<class Value>
class SelectorDecorator: public Selector<Value> {
public:
    typedef Value value_type;

    SelectorDecorator(Selector<Value> *base_selector)
        : base_selector_(base_selector) {
    }

    virtual ~SelectorDecorator() {
        delete base_selector_;
    }

    virtual value_type select() {
        return base_selector_->select();
    }

    virtual bool insert(const value_type &value) {
        return base_selector_->insert(value);
    }

    virtual bool erase(const value_type &value) {
        return base_selector_->erase(value);
    }

    virtual void clear() {
        base_selector_->clear();
    }

    virtual bool empty() const {
        return base_selector_->empty();
    }

    virtual size_t size() const {
        return base_selector_->size();
    }

    virtual void copyTo(std::vector<value_type> &dst) {
        base_selector_->copyTo(dst);
    }

protected:
    Selector<Value> *base_selector() {
        return base_selector_;
    }

private:
    Selector<Value> *base_selector_;
};


template<class Value,
         class RandomGen = RandStdlib,
         class VectorContainer = std::vector<Value>,
         class MapContainer = std::map<Value, size_t> >
class RandomSelector: public Selector<Value> {
public:
	typedef Value value_type;
	typedef RandomGen random_gen_type;

	RandomSelector() {}

	virtual ~RandomSelector() {}

	RandomSelector(const RandomSelector &other)
		: random_gen_(other.random_gen_),
		  map_container_(other.map_container_),
		  vector_container_(other.vector_container_) {

	}

	RandomSelector & operator=(const RandomSelector &other) {
		random_gen_ = other.random_gen_;
		map_container_ = other.map_container_;
		vector_container_ = other.vector_container_;

		return *this;
	}

	virtual value_type select() {
		assert(!empty());

		return vector_container_[random_gen_() % vector_container_.size()];
	}

	virtual bool insert(const value_type &value) {
		std::pair<map_iterator, bool> result = map_container_.insert(
				std::make_pair(value, vector_container_.size()));
		if (!result.second)
			return false;
		vector_container_.push_back(value);
		return true;
	}

	virtual bool erase(const value_type &value) {
		map_iterator it = map_container_.find(value);
		if (it == map_container_.end())
			return false;

		size_t index = it->second;

		if (index != vector_container_.size() - 1) {
			vector_container_[index] =
					vector_container_[vector_container_.size() - 1];
			vector_container_.pop_back();

			map_container_.erase(it);
			map_container_[vector_container_[index]] = index;
		} else {
			vector_container_.pop_back();
			map_container_.erase(it);
		}
		return true;
	}

	virtual void clear() {
		map_container_.clear();
		vector_container_.clear();
	}

	virtual bool empty() const {
		return vector_container_.empty();
	}

	virtual size_t size() const {
		return vector_container_.size();
	}

	virtual void copyTo(std::vector<value_type> &dst) {
		dst.insert(dst.end(), vector_container_.begin(), vector_container_.end());
	}

private:
	typedef MapContainer map_container_type;
	typedef VectorContainer vector_container_type;
	typedef typename map_container_type::iterator map_iterator;

	random_gen_type random_gen_;

	map_container_type map_container_;
	vector_container_type vector_container_;

};


template<class Value,
         class Container = std::set<Value> >
class RoundRobinSelector : public Selector<Value> {
public:
	typedef Value value_type;

	RoundRobinSelector() {
		current_ = container_.begin();
	}

	RoundRobinSelector(const RoundRobinSelector &other)
		: container_(other.container_) {
		current_ = container_.begin();
	}

	virtual ~RoundRobinSelector() {}

	RoundRobinSelector& operator=(const RoundRobinSelector &other) {
		container_ = other.container_;
		current_ = container_.begin();
		return *this;
	}


	virtual value_type select() {
		assert(!container_.empty());

		if (current_ == container_.end())
			current_ = container_.begin();

		return *(current_++);
	}

	virtual bool insert(const value_type &value) {
		return container_.insert(value).second;
	}

	virtual bool erase(const value_type &value) {
		iterator it = container_.find(value);
		if (it == container_.end())
			return false;

		if (it == current_)
			current_++;
		container_.erase(it);
		return true;
	}

	virtual void clear() {
		container_.clear();
		current_ = container_.begin();
	}

	virtual bool empty() const {
		return container_.empty();
	}

	virtual size_t size() const {
		return container_.size();
	}

	virtual void copyTo(std::vector<value_type> &dst) {
		dst.insert(dst.end(), container_.begin(), container_.end());
	}

private:
	typedef Container container_type;
	typedef typename container_type::iterator iterator;

	container_type container_;
	iterator current_;
};


template<class Value,
         class Key,
         class Hash,
         class SubSelector = RandomSelector<Value>,
         class KeySelector = RandomSelector<Key>,
         class Container = std::map<Key, SubSelector> >
class ClassSelector : public Selector<Value> {
public:
	typedef Value value_type;
	typedef Key key_type;
	typedef Hash hash_type;

	typedef SubSelector sub_selector_type;
	typedef KeySelector key_selector_type;

	ClassSelector(const hash_type &hasher = hash_type())
		: hasher_(hasher) {

	}

	virtual ~ClassSelector() {}

	ClassSelector(const ClassSelector &other)
		: container_(other.container_),
		  key_selector_(other.key_selector_),
		  hasher_(other.hasher_),
		  reverse_index_(other.reverse_index_) {
	}

	ClassSelector& operator=(const ClassSelector &other) {
		container_ = other.container_;
		key_selector_ = other.key_selector_;
		hasher_ = other.hasher_;
		reverse_index_ = other.reverse_index_;

		return *this;
	}

	virtual value_type select() {
		assert(!container_.empty());
		assert(!key_selector_.empty());

		key_type key = key_selector_.select();
		iterator it = container_.find(key);

		assert(it != container_.end());

		return it->second.select();
	}

	virtual bool insert(const value_type &value) {
		typename reverse_type::iterator kit = reverse_index_.find(value);
		if (kit != reverse_index_.end())
			return false;

		key_type key = hasher_(value);
		container_[key].insert(value);

		reverse_index_[value] = key;
		key_selector_.insert(key);
		return true;
	}

	virtual bool erase(const value_type &value) {
		typename reverse_type::iterator kit = reverse_index_.find(value);
		if (kit == reverse_index_.end())
			return false;

		key_type key = kit->second;
		reverse_index_.erase(kit);

		iterator it = container_.find(key);
		assert(it != container_.end());

		it->second.erase(value);
		if (!it->second.empty())
			return true;

		container_.erase(it);
		key_selector_.erase(key);
		return true;
	}

	virtual void clear() {
		container_.clear();
		reverse_index_.clear();
		key_selector_.clear();
	}

	virtual bool empty() const {
		return container_.empty();
	}

	virtual size_t size() const {
		size_t result = 0;
		for (const_iterator it = container_.begin(), ie = container_.end();
				it != ie; ++it) {
			result += it->second.size();
		}
		return result;
	}

#if 0
	void sizeBreakdown(std::map<key_type, ssize_t> &size_map) const {
		size_map.clear();
		for (const_iterator it = container_.begin(), ie = container_.end();
				it != ie; ++it) {
			size_map[it->first] = it->second.size();
		}
	}
#endif

	virtual void copyTo(std::vector<value_type> &dst) {
		for (iterator it = container_.begin(), ie = container_.end();
				it != ie; ++it) {
			it->second.copyTo(dst);
		}
	}

	hash_type &hasher() {
		return hasher_;
	}

	const key_selector_type &key_selector() const {
		return key_selector_;
	}

	virtual void updateWeights() {
		key_selector_.updateWeights();

		for (iterator it = container_.begin(), ie = container_.end();
				it != ie; ++it) {
			it->second.updateWeights();
		}
	}

private:
	typedef Container container_type;
	typedef typename container_type::iterator iterator;
	typedef typename container_type::const_iterator const_iterator;

	typedef std::map<value_type, key_type> reverse_type;

	container_type container_;
	key_selector_type key_selector_;

	hash_type hasher_;
	reverse_type reverse_index_;
};


template<class Value,
         class Weight,
         class RandomGen = RandStdlib>
class WeightedRandomSelector: public Selector<Value> {
public:
	typedef Value value_type;
	typedef Weight weight_type;
	typedef RandomGen random_gen_type;

	WeightedRandomSelector() { }
	virtual ~WeightedRandomSelector() { }

	WeightedRandomSelector(const WeightedRandomSelector &other)
		: random_gen_(other.random_gen_),
		  weight_(other.weight_),
		  set_container_(other.set_container_) {
		for (typename set_container_type::iterator it = set_container_.begin(),
				ie = set_container_.end(); it != ie; ++it) {
			pdf_container_.insert(*it, other.pdf_container_.getWeight(*it));
		}
	}

	WeightedRandomSelector &operator=(const WeightedRandomSelector &other) {
		random_gen_ = other.random_gen_;
		weight_ = other.weight_;
		set_container_ = other.set_container_;

		for (typename set_container_type::iterator it = set_container_.begin(),
				ie = set_container_.end(); it != ie; ++it) {
			pdf_container_.insert(*it, other.pdf_container_.getWeight(*it));
		}

		return *this;
	}

	virtual value_type select() {
		assert(!empty());

		double rvalue = double(random_gen_() % RAND_MAX) / RAND_MAX;

		return *set_container_.find(pdf_container_.choose(rvalue));
	}

	virtual bool insert(const value_type &value) {
		if (set_container_.insert(value).second) {
			double w = weight_(value);
			pdf_container_.insert(value, w);
			return true;
		}
		return false;
	}

	virtual void updateWeights() {
		for (typename set_container_type::iterator it = set_container_.begin(),
				ie = set_container_.end(); it != ie; ++it) {
			double w = weight_(*it);
			pdf_container_.update(*it, w);
		}
	}

	virtual bool erase(const value_type &value) {
		if (set_container_.erase(value)) {
			pdf_container_.remove(value);
			return true;
		}
		return false;
	}

	virtual void clear() {
		for (typename set_container_type::iterator it = set_container_.begin(),
				ie = set_container_.end(); it != ie; ++it) {
			pdf_container_.remove(*it);
		}
		set_container_.clear();
	}

	virtual bool empty() const {
		return set_container_.empty();
	}

	virtual size_t size() const {
		return set_container_.size();
	}

	virtual void copyTo(std::vector<value_type> &dst) {
		dst.insert(dst.end(), set_container_.begin(), set_container_.end());
	}

private:
	typedef klee::DiscretePDF<Value> pdf_container_type;
	typedef std::set<Value> set_container_type;

	random_gen_type random_gen_;
	weight_type weight_;

	// TODO: Replace the set with a value -> weight map

	pdf_container_type pdf_container_;
	set_container_type set_container_; // We need the set for the iterators
};


template<class Value,
         class SubSelector>
class GenerationalSelector : public Selector<Value> {
public:
	typedef Value value_type;

	GenerationalSelector() : bin_index_(0) {}
	virtual ~GenerationalSelector() {}

	GenerationalSelector(const GenerationalSelector &other)
		: bins_(other.bins_),
		  bin_index_(other.bin_index_) {

	}

	GenerationalSelector &operator=(const GenerationalSelector &other) {
		bins_[0] = other.bins_[0];
		bins_[1] = other.bins_[1];
		bin_index_ = other.bin_index_;

		return *this;
	}

	virtual value_type select() {
		assert(!empty());

		if (bins_[bin_index_].empty()) {
			bin_index_ = 1 - bin_index_;
		}

		return bins_[bin_index_].select();
	}

	virtual bool insert(const value_type &value) {
		// XXX: Should we remove the item in the current bin?
		//bins_[bin_index_].erase(value);
		return bins_[1 - bin_index_].insert(value);
	}

	virtual bool erase(const value_type &value) {
		if (bins_[bin_index_].erase(value))
			return true;

		return bins_[1 - bin_index_].erase(value);
	}

	virtual void clear() {
		bins_[0].clear();
		bins_[1].clear();
		bin_index_ = 0;
	}

	virtual bool empty() const {
		return bins_[0].empty() && bins_[1].empty();
	}

	virtual size_t size() const {
		return bins_[0].size() + bins_[1].size();
	}

	virtual void copyTo(std::vector<value_type> &dst) {
		bins_[bin_index_].copyTo(dst);
		bins_[1 - bin_index_].copyTo(dst);
	}

private:
	typedef SubSelector subselector_type;

	subselector_type bins_[2];
	int bin_index_;
};


////////////////////////////////////////////////////////////////////////////////


template<class Value>
class Selector2 : boost::noncopyable {
public:
    typedef Value value_type;

    virtual ~Selector2() {}

    virtual bool update(const value_type &value) = 0;
    virtual bool remove(const value_type &value) = 0;

    virtual value_type select() = 0;
};


template<class Value,
         class QueueContainer,
         class MapContainer>
class QueuedSelector: public Selector2<Value> {
public:
    typedef Value value_type;

    QueuedSelector() {}
    virtual ~QueuedSelector() {}

    virtual bool update(const value_type &value) {
        std::pair<typename map_type::iterator, bool> result = map_.insert(
                std::make_pair(value, queue_.end()));
        if (!result.second) {
            queue_.erase(result.first->second);
            queue_.push_front(value);
            result.first->second = queue_.begin();
            return false;
        }

        queue_.push_front(value);
        result.first->second = queue_.begin();
        return true;
    }

    virtual bool remove(const value_type &value) {
        map_iterator it = map_.find(value);
        if (it == map_.end())
            return false;

        queue_.erase(it->second);
        map_.erase(it);
        return true;
    }

protected:
    typedef QueueContainer queue_type;
    typedef MapContainer map_type;
    typedef typename map_type::iterator map_iterator;

    queue_type &queue() {
        return queue_;
    }

    map_type &map() {
        return map_;
    }

private:
    queue_type queue_;
    map_type map_;
};


template<class Value,
         class QueueContainer = std::list<Value>,
         class MapContainer = std::map<Value, typename QueueContainer::iterator> >
class BFSSelector: public QueuedSelector<Value, QueueContainer, MapContainer> {
public:
    typedef Value value_type;

    BFSSelector() {}
    virtual ~BFSSelector() {}

    virtual value_type select() {
        if (this->queue().empty())
            return value_type();

        return this->queue().back();
    }
};


template<class Value,
         class QueueContainer = std::list<Value>,
         class MapContainer = std::map<Value, typename QueueContainer::iterator> >
class DFSSelector: public QueuedSelector<Value, QueueContainer, MapContainer> {
public:
    typedef Value value_type;

    DFSSelector() {}
    virtual ~DFSSelector() {}

    virtual value_type select() {
        if (this->queue().empty())
            return value_type();

        return this->queue().front();
    }
};


template<class Value>
class StickySelector : public Selector2<Value> {
public:
    typedef Value value_type;
    typedef scoped_ptr<Selector2<Value> > selector_ref;

    StickySelector(Selector2<Value> *base) : base_(base) {

    }

    virtual ~StickySelector() { }

    virtual bool update(const value_type &value) {
        return base_->update(value);
    }

    virtual bool remove(const value_type &value) {
        if (value == sticky_value_) {
            sticky_value_ = value_type();
        }
        return base_->remove(value);
    }

    virtual value_type select() {
        if (sticky_value_ == value_type()) {
            sticky_value_ = base_->select();
        }
        return sticky_value_;
    }

private:
    selector_ref base_;
    value_type sticky_value_;
};


template<class Value,
         class RandomGen = RandStdlib,
         class VectorContainer = std::vector<Value>,
         class MapContainer = std::map<Value, size_t> >
class RandomSelector2: public Selector2<Value> {
public:
    typedef Value value_type;
    typedef RandomGen random_gen_type;

    RandomSelector2() {}

    virtual ~RandomSelector2() {}


    virtual bool update(const value_type &value) {
        std::pair<map_iterator, bool> result = map_container_.insert(
                std::make_pair(value, vector_container_.size()));
        if (!result.second)
            return false;
        vector_container_.push_back(value);
        return true;
    }

    virtual bool remove(const value_type &value) {
        map_iterator it = map_container_.find(value);
        if (it == map_container_.end())
            return false;

        size_t index = it->second;

        if (index != vector_container_.size() - 1) {
            vector_container_[index] =
                    vector_container_[vector_container_.size() - 1];
            vector_container_.pop_back();

            map_container_.erase(it);
            map_container_[vector_container_[index]] = index;
        } else {
            vector_container_.pop_back();
            map_container_.erase(it);
        }
        return true;
    }

    virtual value_type select() {
        if (vector_container_.empty())
            return value_type();

        return vector_container_[random_gen_() % vector_container_.size()];
    }

private:
    typedef MapContainer map_container_type;
    typedef VectorContainer vector_container_type;
    typedef typename map_container_type::iterator map_iterator;

    random_gen_type random_gen_;

    map_container_type map_container_;
    vector_container_type vector_container_;

};


} // namespace s2e


#endif /* ROUNDROBIN_H_ */
