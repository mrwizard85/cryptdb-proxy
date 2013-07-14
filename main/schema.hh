#pragma once

#include <util/onions.hh>

#include <parser/embedmysql.hh>
#include <parser/stringify.hh>
#include <main/CryptoHandlers.hh>
#include <main/Translator.hh>
#include <main/enum_text.hh>
#include <main/dbobject.hh>
#include <string>
#include <map>
#include <list>
#include <iostream>

class Analysis;
struct FieldMeta;
/**
 * Field here is either:
 * A) empty string, representing any field or
 * B) the field that the onion is key-ed on. this
 *    only has semantic meaning for DET and OPE
 */
typedef std::pair<SECLEVEL, FieldMeta *> LevelFieldPair;

typedef std::map<SECLEVEL, FieldMeta *> LevelFieldMap;
typedef std::pair<onion, LevelFieldPair> OnionLevelFieldPair;
typedef std::map<onion, LevelFieldPair> OnionLevelFieldMap;

// onion-level-key: all the information needed to know how to encrypt a
// constant
class OLK {
public:
    OLK(onion o, SECLEVEL l, FieldMeta * key) : o(o), l(l), key(key) {}
    OLK() : o(oINVALID), l(SECLEVEL::INVALID), key(NULL) {}
    onion o;
    SECLEVEL l;
    FieldMeta * key; // a field meta is a key because each encryption key
                     // ever used in CryptDB belongs to a field; a field
                     // contains the encryption and decryption handlers
                     // for its keys (see layers)
    bool operator<(const OLK & olk ) const {
        return (o < olk.o) || ((o == olk.o) && (l < olk.l));
    }
    bool operator==(const OLK & olk ) const {
        return (o == olk.o) && (l == olk.l);
    }
};

const OLK PLAIN_OLK = OLK(oPLAIN, SECLEVEL::PLAINVAL, NULL);

// TODO: A bit HACKy. Could be cleaned up with RTTI.
// TODO: This really should be templated, those exceptions are _why_
// templates.
//       > Can't template because it requires templating AbstractMeta
//         and we want all AbstractMetaz to be the same type. Otherwise
//         AbstractMeta::children will need to know what type the 'children'
//         use for keys.
class MetaKey {
    const std::string *s;
    const onion *o;

public:
    MetaKey(std::string s) : s(new std::string(s)), o(NULL) {}
    MetaKey(onion o) : s(NULL), o(new onion(o)) {}
    ~MetaKey() {
        /*
        if (s) delete s;
        if (o) delete o;
        */
    }

    bool operator <(const MetaKey &rhs) const
    {
        if (o && rhs.o) {
            return *o < *rhs.o;
        } else if (s && rhs.s) {
            return *s < *rhs.s;
        } else {
            throw CryptDBError("MetaKey cannot support this operation!");
        }
    }

    bool operator ==(const MetaKey &rhs) const
    {
        if (o && rhs.o) {
            return *o == *rhs.o;
        } else if (s && rhs.s) {
            return *s == *rhs.s;
        } else {
            throw CryptDBError("MetaKey cannot support this operation!");
        }
    }

    std::string getString() const
    {
        assert(s);
        return *s;
    }
};

// > TODO: template child and parent type (possibly key type as well).
//   This would allow us to remove boilerplate for children of *Meta class.
// > TODO: Mage getDatabaseID() protected by templating on the Concrete type
//   and making it a friend.
struct AbstractMeta : public DBObject {
    // TODO: Remove default constructor.
    AbstractMeta() {}
    virtual ~AbstractMeta()
    {
        auto cp = children;
        children.clear();

        for (auto it : cp) {
            delete it.second;
        }
    }
    // Virtual constructor to deserialize from embedded database.
    template <typename ConcreteMeta>
        static ConcreteMeta *deserialize(std::string serial);
    bool childExists(const MetaKey &key) const;
    AbstractMeta *getChild(const MetaKey &key) const;
    MetaKey getKey(const AbstractMeta *const child) const;
    virtual bool addChild(const MetaKey &key, AbstractMeta *meta);
    bool replaceChild(const MetaKey &key, AbstractMeta *meta);
    virtual bool destroyChild(const MetaKey &key);
    std::vector<AbstractMeta *> fetchChildren(Connect *e_conn);
    // FIXME: Use rtti.
    virtual std::string typeName() const = 0;

    std::map<MetaKey, AbstractMeta *> children;
};

/*
 * The name must be unique as it is used as a unique identifier when
 * generating the encryption layers.
 */
// TODO: Semantically enforce that OnionMeta can not have children.
typedef struct OnionMeta : AbstractMeta {
    // TODO: Private.
    std::vector<EncLayer *> layers; //first in list is lowest layer

    // New.
    OnionMeta(onion o) 
        : onionname(getpRandomName() + TypeText<onion>::toText(o)) {};
    // Restore.
    /*
    OnionMeta(std::string name)
        : onionname(name) {}
    */
    OnionMeta(std::string serial);
    std::string serialize(const DBObject &parent) const;
    std::string getAnonOnionName() const;
    // FIXME: Use rtti.
    std::string typeName() const {return "onionMeta";}

    SECLEVEL getSecLevel() {
        assert(layers.size() > 0);
        return layers.back()->level();
    }

private:
    const std::string onionname;
} OnionMeta;

struct TableMeta;
//TODO: FieldMeta and TableMeta are partly duplicates with the original
// FieldMetadata an TableMetadata
// which contains data we want to add to this structure soon
typedef struct FieldMeta : public AbstractMeta {
    const std::string fname;
    bool has_salt; //whether this field has its own salt
    const std::string salt_name;
    onionlayout onion_layout;

    std::map<onion, OnionMeta *> onions;

    // New field.
    FieldMeta(std::string name, Create_field *field, AES_KEY *mKey);
    // Recovering field from proxy db.
    FieldMeta(std::string name, bool has_salt, 
              std::string salt_name, onionlayout onion_layout)
        : fname(name), has_salt(has_salt), salt_name(salt_name),
          onion_layout(onion_layout) {}
    FieldMeta(std::string serial);
    ~FieldMeta();

    std::string serialize(const DBObject &parent) const;
    std::string stringify() const;

    std::string getSaltName() const {
        assert(has_salt);
        return salt_name;
    }

    SECLEVEL getOnionLevel(onion o) const {
        auto it = onions.find(o);
        if (it == onions.end()) return SECLEVEL::INVALID;

        return it->second->getSecLevel();
    }

    bool setOnionLevel(onion o, SECLEVEL maxl) {
        SECLEVEL current_sec_level = onions[o]->getSecLevel();
        if (current_sec_level > maxl) {
            while (onions[o]->layers.size() != 0 && onions[o]->layers.back()->level() != maxl) {
                onions[o]->layers.pop_back();
            }
            return true;
        }
        return false;
    }

    bool isEncrypted() {
        return ((onions.size() != 1) ||  (onions.find(oPLAIN) == onions.end()));
    }

    // FIXME: Use rtti.
    std::string typeName() const {return "fieldMeta";}
} FieldMeta;

// TODO: Put const back.
typedef struct TableMeta : public AbstractMeta {
    std::list<MetaKey> fieldNames;     //in order field names
    bool hasSensitive;
    bool has_salt;
    std::string salt_name;
    std::string anon_table_name;

    // Restore old TableMeta.
    TableMeta(bool has_sensitive, bool has_salt, std::string salt_name,
              std::string anon_table_name,
              std::map<std::string, std::string> index_map)
        : hasSensitive(has_sensitive), has_salt(has_salt),
          salt_name(salt_name), anon_table_name(anon_table_name),
          index_map(index_map) {}

    // New TableMeta.
    TableMeta(bool has_sensitive, bool has_salt,
              std::map<std::string, std::string> index_map)
        : hasSensitive(has_sensitive), has_salt(has_salt),
          salt_name("tableSalt_" + getpRandomName()),
          anon_table_name("table_" + getpRandomName()),
          index_map(index_map) {}
    TableMeta(std::string serial);

    std::string serialize(const DBObject &parent) const;
    bool addChild(const MetaKey &key, AbstractMeta *meta);
    std::string getAnonTableName() const;
    bool destroyChild(const MetaKey &key);
    // FIXME: Use rtti.
    std::string typeName() const {return "tableMeta";}

    friend class Analysis;

protected:
    std::string addIndex(std::string index_name); 
    std::string getAnonIndexName(std::string index_name) const;
    std::string getIndexName(std::string anon_index_name) const;
    bool destroyIndex(std::string index_name);
   
private:
    std::map<std::string, std::string> index_map;
} TableMeta;


// FIXME: Inherit from AbstractMeta.
// AWARE: Table/Field aliases __WILL NOT__ be looked up when calling from
// this level or below. Use Analysis::* if you need aliasing.
typedef struct SchemaInfo : public AbstractMeta {
    SchemaInfo() {;}
    ~SchemaInfo();

    std::string typeName() const {return "SchemaInfo";}

    friend class Analysis;

private:
    std::map<std::string, TableMeta *> tableMetaMap;

    // These functions do not support Aliasing, use Analysis::getTableMeta
    // and Analysis::getFieldMeta.
    FieldMeta * getFieldMeta(const std::string & table,
                             const std::string & field) const;
    std::string serialize(const DBObject &parent) const {
        throw CryptDBError("SchemaInfo can not be serialized!");
    }
} SchemaInfo;

