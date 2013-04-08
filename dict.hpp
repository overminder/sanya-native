// In order to utilize our allocator

template <typename SubClass, typename Key, typename Val,
          typename HashFunc, typename EqFunc, typename AllocFunc>
class Dict {
 public:
  void insert(Key key, Val val) {
    bool found;
    intptr_t hash = HashFunc(key);
    Entry *entry = lookupEntry(key, hash, &found);
    if (!found) {
      // Rehash when 1/4 full
      if ((occupied << 2) > buckets) {
        static_cast<SubClass *>(this)->rehash(occupied << 2);
        insert(key, val);
        return;
      }

      ++occupied;
      entry->status = filled;
      entry->hash = hash;
    }
    entry->val = val;
  }

  Val lookup(Key key, bool *found) {
    intptr_t hash = HashFunc(key);
    Entry *entry = lookupEntry(key, hash, found);
    return entry->val;
  }

  Key lookupKey(Key key, bool *found) {
    intptr_t hash = HashFunc(key);
    Entry *entry = lookupEntry(key, hash, found);
    return entry->key;
  }

  void remove(Key key, bool *found) {
    intptr_t hash = HashFunc(key);
    Entry *entry = lookupEntry(key, hash, found);
    if (*found) {
    }
    return entry->val;
  }

 private:

  // Copied from `c++/4.5/backward/hashtable'
  enum { kNumBuckets = 29 };

  static const uintptr_t bucketList[kNumBuckets] = {
    5ul,          53ul,         97ul,         193ul,       389ul,
    769ul,        1543ul,       3079ul,       6151ul,      12289ul,
    24593ul,      49157ul,      98317ul,      196613ul,    393241ul,
    786433ul,     1572869ul,    3145739ul,    6291469ul,   12582917ul,
    25165843ul,   50331653ul,   100663319ul,  201326611ul, 402653189ul,
    805306457ul,  1610612741ul, 3221225473ul, 4294967291ul
  };

  enum EntryStatus {
    Empty,
    Filled,
    TombStone
  };

  struct Entry {
    Key key;
    intptr_t hash;
    Val val;
    EntryStatus status;
  };

  Entry *lookupEntry(Key key, intptr_t hash, bool *found) {
    Entry *lastTomb = NULL;
    for (intptr_t i = 0; i < buckets; ++i) {
      intptr_t ix = (hash + i * i) % buckets;
      Entry *got = items + ix;
      if (got->status == Empty) {
        if (found) {
          *found = false;
        }
        return got;
      }
      else if (got->status == TombStone) {
        lastTomb = lastTomb ? lastTomb : got;
      }
      else if (got->status == Filled && got->hash == hash &&
               EqFunc(got->key, key)) {
        if (found) {
          *found = true;
        }
        return got;
      }
    }
    if (found) {
      *found = false;
    }
    return lastTomb;
  }

  intptr_t buckets;
  intptr_t occupied;
  Entry *items;
};

