/*
 * AccessManager.h
 *
 *
 */

#ifndef ACCESSMANAGER2_H_
#define ACCESSMANAGER2_H_

#include "util.h"



#include <string>
#include <map>
#include <set>
#include <list>
#include "Connect.h"
#include "CryptoManager.h"

using namespace std;

/*
 * This class maintains the flow of access based on user-annotated schema.
 * 
 * a user refers to a physical user
 * a principle refers to a field in the database that has encrypted entries and/or has other fields encrypted for it
 * a generic principle is a collection of principle that all refer to the same values
 *
 */


//TODO: fix PRINCVALUE to be application dependent
#define PRINCTYPE "varchar(255)"
#define PRINCVALUE "varchar(255)"
#define TESTING 1
#define THRESHOLD 100

typedef struct Prin {
	//the name of the principle (either a generic or a principle)
	string type;
	//the value of the principle
	string value;
	//the generic this Prin is part of (if it's already a generic, this is the same)
	string gen;
	//for example, the Prin describing g.gid = 5, will have type g.gid and value 5 and gen 0001ggid

	Prin() {}

	Prin(string type, string value) {
		this->type = type;
		this->value = value;
		this->gen = "";
	}

	bool operator<(const Prin &a) const {
		if (gen.compare(a.gen) != 0) {
			return (gen < a.gen);
		}
		return (value < a.value);
	};

	bool operator==(const Prin &a) const {
		return ((gen.compare(a.gen) == 0) && (value.compare(a.value) == 0));
	}

	bool operator!=(const Prin &a) const {
		return ((gen.compare(a.gen) != 0) || (value.compare(a.value) != 0));
	}

	string toString() {
		return type + " " + value;
	}
} Prin;

typedef struct PrinKey {
	unsigned char * key;
	unsigned int len;
	//principles currently holding the keys to access this key
	std::set<Prin> principles_with_access;

	bool operator==(const PrinKey &a) const {
		if (a.len != len) {
			return false;
		}
		return isEqual(key,a.key,len);
	}
} PrinKey;


class MetaAccess {
public:
	MetaAccess(Connect * c, bool verb);

	//defines two names that refer to the same principle
	void addEquals(string princ1, string princ2);

	//princHasAccess has access to princAccessible
	void addAccess(string princHasAccess, string princAccessible);

	//adds princ to the set of principles which can give a password
	void addGives(string princ);

	//prints out all the information stored in memory
	void PrintMaps();

	//populates the database with the tables for each access link
	//this method should only be called once, after all the access and equals links have been added
	int CreateTables();

	bool CheckAccess();

	//drops all the tables create in CreateTables from the database
	int DeleteTables();

	//all four of the following functions only go one step along the access graph
	//returns the set of all principles (not generics) that can immediately access princ
	//does not include princ or any type which princ equals
	std::set<string> getTypesAccessibleFrom(string princ);
	//returns the set of all principles (not generics) that princ has immediate access to
	//does not include princ or any type which princ equals
	std::set<string> getTypesHasAccessTo(string princ);
	//returns the set of all generics that can immediately access gen
	//includes gen
	std::set<string> getGenAccessibleFrom(string gen);
	//returns the set of all generics that gen has immediate access to
	//includes gen
	std::set<string> getGenHasAccessTo(string gen);

	//returns the set of all the principles (not generics) that are equal to principle princ
	std::set<string> getEquals(string princ);

	//returns true if princ gives passsword
	//requires: princ to be type not generic
	bool isGives(string princ);
	//same as isGives, but takes generic as input
	bool isGenGives(string gen);

	//requires: hasAccess and accessTo to be generics
	string getTable(string hasAccess, string accessTo);

	//gives generic for princ
	//requires: princ is already stored in MetaAccess
	string getGenericPublic(string princ);

	string publicTableName();

	void finish();

	virtual ~MetaAccess();

private:
	string getGeneric(string princ);
	string createGeneric(string clean_princ);

	//requires unsanitized to be of the form table.field (exactly one '.')
	string sanitize(string unsanitized);
	string unsanitize(string sanitized);
	std::set<string> unsanitizeSet(std::set<string> sanitized);

	//user-supplied principle to generic principle
	map<string, string> prinToGen;
	//generic principle to user-supplied principles
	map<string, std::set<string> > genToPrin;

	//maps the principles which have access to things to the principles they have access to
	map<string, std::set<string> > genHasAccessToList;
	//maps the principles which are accessible by things to the principles they are accessible by
	map<string, std::set<string> > genAccessibleByList;

	//keeps track of principles which can give passwords
	std::set<string> givesPsswd;

	//maps a pair of generics to a table
	//requires that first string is hasAccess, second accessTo
	map<string, map<string,string> > genHasAccessToGenTable;

	Connect * conn;
	bool VERBOSE;
	string table_name;
	string public_table;
	unsigned int table_num;
};


class KeyAccess {
public:
	KeyAccess(Connect * connect);

	//meta access functions
	//defines two names that refer to the same principle
	int addEquals(string prin1, string prin2);
	//princHasAccess has access to princAccessible
	int addAccess(string hasAccess, string accessTo);
	//adds princ to the set of principles which can give a password
	int addGives(string prin);
	//this method should only be called once, after all the access and equals links have been added
	int CreateTables();
	//returns the set of all principles (not generics) that can immediately access princ
	//does not include princ or any type which princ equals
	std::set<string> getTypesAccessibleFrom(string princ);
	//returns the set of all principles (not generics) that princ has immediate access to
	//does not include princ or any type which princ equals
	std::set<string> getTypesHasAccessTo(string princ);
	//returns the set of all generics that can access gen
	//does include gen
	std::set<string> getGenAccessibleFrom(string gen);
	//returns the set of all generics that gen has access to
	//does include gen
	std::set<string> getGenHasAccessTo(string gen);
	//returns the set of all the principles (not generics) that are equal to principle princ
	std::set<string> getEquals(string princ);

	//get generic public
	string getGeneric(string prin);

	void Print();

	bool isType(string type);

	//add a key for the principle hasAccess to access principle accessTo
	//input: both Prins are expected to not be generics
	//requires: if hasAccess is a givesPsswd, it must exist (KeyAccess cannot generate password derived keys)
	//return: 0  if key is inserted successfully
	//        1  if key already existed
	//        <0 if an error occurs
	int insert(Prin hasAccess, Prin accessTo);

	//reverse insert on a particular hasAccess -> accessTo link
	//input: both Prins are expected not to be generics
	int remove(Prin hasAccess, Prin accessTo);

	//returns the symmetric key for the principle Prin, if held
	//returns keys of length AES_KEY_BYTES
	unsigned char * getKey(Prin prin);

	//returns the symmetric key for the principle Prin, if held
	//returns keys of length AES_KEY_BYTES
	PrinKey getPrinKey(Prin prin);

	//returns the public key for the principle prin, NULL if prin is not found
	PKCS * getPublicKey(Prin prin);
	//returns the secret key for the principle prin
	//requires: access to prin's symmetric key
	PrinKey getSecretKey(Prin prin);

	//inserts a givesPsswd value
	//if the value has access to other principles, all those keys are accessed and decrypted
	//requires: psswd be of length AES_KEY_BYTES
	int insertPsswd(Prin gives, unsigned char * psswd);


	//removes a givesPsswd value
	//if the value is holding keys to other principles that no other inserted givesPsswd value has access to, the keys are dropped from keys
	int removePsswd(Prin prin);

	void finish();

	virtual ~KeyAccess();

private:
	//describes keys currently held by the proxy
	map<Prin, PrinKey> keys;
	//describes uncached keys accessible in the database
	// string is the gen of the accessTo key; principle Prin has access to the key
	map<string, std::set<Prin> > uncached_keys;

	//describe all chains disconnected from a physical principle
	map<Prin, std::set<Prin> > orphanToParents;
	map<Prin, std::set<Prin> > orphanToChildren;
	//the MetaAccess that described the possible access links
	MetaAccess * meta;
	CryptoManager * crypt_man;
	Connect * conn;
	bool VERBOSE;
	bool meta_finished;

	//creates PrinKey
	//requires: hasAccess and accessTo to have gen field set
	PrinKey buildKey(Prin hasAccess, unsigned char * sym_key, int length_sym);

	//adds prin to the map keys
	//if keys already holds this keys, the principles_with_access sets are merged
	//returns: 0  if key added successfully
	//         1  if prin already has this key
	//         <0 if prin already has a different key
	int addToKeys(Prin prin, PrinKey key);

	//removes prin to the map keys
	//returns: 0  if key removed sucessfully
	//         1  if key does not exist
	//         <0 for other errors (none in place yet...)
	int removeFromKeys(Prin prin);

	//returns result if prin in table_name
	//requires: prin.gen to exist
	vector<vector<string> > * Select(std::set<Prin> &prin_set, string table_name, string column);
	int SelectCount(std::set<Prin> &prin_set, string table_name);

	//removes the row from the access table hasAccess->accessTo that contains the values of hasAccess.value and accessTo.value
	//requires: hasAccess.gen and accessTo.gen to exits
	//returns: 0  if row removed sucessfully
	//         <0 if an error occurs
	int RemoveRow(Prin hasAccess, Prin accessTo);

	//generates a public/secret asymmetric key pair for principle prin, encrypts the secret key with key prin_key and stores the public key and encrypted secret key in the public keys table
	//requires prin_key with key and len
	int GenerateAsymKeys(Prin prin, PrinKey prin_key);

	//removes orphan, and all descendants of orphan from the orphan graph
	int removeFromOrphans(Prin orphan);

	//returns: str_encrypted_key decrypted symmetrically with key_for_decrypting
	//         principles_with_access is empty
	PrinKey decryptSym(string str_encrypted_key, unsigned char * key_for_decrypting, string str_salt);

	PrinKey decryptAsym(string str_encrypted_key, unsigned char * secret_key, int secret_key_length);

	bool isInstance(Prin prin);
	bool isOrphan(Prin prin);




#if TESTING
public:
#endif
	//if prin has uncached keys, finds and returns them
	//if key is found in keys, prints an error message
	//requres: all keys in uncached_keys to have their principles still logged on
	//returns: PrinKey for prin is exists
	//         empty PrinKey if prin does not have an accessible key
	PrinKey getUncached(Prin prin);

	//Bredth First Search for generics start has access to
	list<string> BFS_hasAccess(Prin start);

	//Depth First Search for generics start has access to
	list<string> DFS_hasAccess(Prin start);

	int DeleteTables();

};

#endif