Duckboard NG
============

Goals
-----
The aim of Duckboard is to build a kanban board, issue tracker, bug tracker,
project management tool with distinct properties:

* Scalability in the sense that the same tool can be used to operate your
  personal todo list or manage a whole megacorporation. And that the former can
  seamlessly grow into the second.
* Work in a federated fashion, that it is not required to centralize or merge
  instances but that a user can work across them, like you navigate from one
  webpage to the next.
* Be simple, yet powerful. Like math or a language: you get few tools but you
  can combine them in endless possibilities.
* API-first: to allow automation, integration and extension. To allow an
  ecosystem to grow around it.
* To be free as in speech

Concepts
--------

### Spheres and Scopes

A sphere in the duckboard system is a single duckboard instance, a cluster of
them or even a distributed system of such clusters. But always under control /
ownership of the same authority. Think 'sphere of influence'. If you set up a
duckboard, you administrate it and ultimately control it, even if other people
are using it. If a duckboard is owned by someone else and you cannot reconfigure
it then it's a different sphere (you may be able to change items in it though).
A sphere is identified by a URI, e.g. https://megacorp.com/db.

Within a sphere, any number of scopes may exist. These may differ in
configuration, and control over a scope may be delegated by the sphere owner.
But ultimately all scopes are still owned and controlled by whoever owns the
sphere they are in. Scopes are typically used to separate teams or projects. A
scope is identified by a URI that is an extension of the sphere URI, e.g. 
https://megacorp.com/db/rnd. Scopes cannot be nested.

### Items

Each scope can contain a number of items, which are what a user will mostly
interact with. Items represent tasks, issues etc. Items are highly variable over
time, and have a history. In contrast, a scope may be deleted, but there is no
history of scope addition/removal in duckboard.

Items are dynamic objects that can have arbitrary properties. To some degree
this is defined by the configuration of the scope, but there is no fixed schema
across items, not even within a scope. The property values can be scalar strings
or numbers, arrays of values or even key-value objects themselves. A lot like
JSON. Typical properties are a title, description, a state, owner, due date,
tags etc.

Each item is identified by an ID unique to the scope, and therefore by a URI,
e.g. https://megacorp.com/db/rnd/item/1234. IDs are assigned by duckboard according
to a scheme that may differ from scope to scope, and are not necessarily
guaranteed to be sequential, numeric or anything. Just treat them as unique
strings.

Duckboard keeps a history of each item, with all changes to it as deltas. A lot
like a version control system. A change is reccorded as cahnges to top-level
proeprties of the object and can be a setting for a scalar, add/removes for
arrays and insertion/removal for hashes. This history of deltas is conceptually
only, internally duckboard will use a data structure that is more efficient and
does not require aggregating all changes since the beginning of time to get the
current state.

### Archiving

The ways to query for items within duckboards are fairly flexible, which means
it is impossible to optimize for all of them. There are some underlying
assumptions that duckboard optimizes for, one of them being that there are few
total items per scope. This makes sense because a scope with thousands of items
is hard to digest for a human as well. A scope could very well however contain
items never seen by a human because they are for example filtered out by a state
"done". Over the years so many of these could accumulate that they would
negatively affect performance. Duckboard therefore has the hardwired concept of
a special "archived" property which is a boolean and always present. Items start
their life having this set to false, and at some point it could be set to true,
typically based on some heuristic like "X days after state got set to 'done'". 

By default queries for items in duckboard only return items where archived =
false, if you want all of them for e.g. a historic view or when looking for an
item by ID, you need to specify that you also want archived items. Internally
this allows duckboard to separate the archived from the non-archived items, and
thereby reduce the number of items looked at during regular queries to an
upper limit. Note that querying for this property is done in a special way, not
through the regular query language described in the next section.

XXX how can an item become archived?

### Querying Items

The core of most duckboard functions is the ability to iterate items that match
certain criteria. These criteria are expressed in a simple query language that
can access all properties except the special "archived" property mentioned
above.

XXX grammar

### Boards

XXX

### Ordering Items

XXX

### Relations

XXX

### Feature Plugins

A duckboard sphere can, and typically will, have a number of feature plugins
installed that can then be enabled/disabled and configured at the scope level.
Enabled features can modify the behavior and/or appearance of a duckboard. Most
actual functionality is provided by feature plugins! Please see below for a list
of standard feature plugins

XXX

### Nodes, Clusters and Replication Sets

A node is a service instance that provides a sphere and is reachable under a
sphere URI, a process running and answering requests.

A Cluster is a set of nodes that provide the same sphere and are reachable under
the same URL. So the hostname must resolve to the IPs of all nodes in the
cluster. A Cluster is guaranteed to behave like a single node, but may provide
fault tolerance, failover, and higher performance through load balancing and/or
sharding.

Replication allows a set of nodes or clusters to be reachable under the same URL
and serve the same sphere, but loosens the guarantees that a cluster provides. A
Replication set may move changes asyncronously between members, violating
read-after-write guarantees on different member nodes. On the upside the async
replication allows for quicker responses as no coordination needs to happen over
slow links. Replication therefore enables geo-redundancy and offline operations.

Technical Design
----------------

### Item Representation

As outlined above, each item is conceptually a list of deltas with a timestamp.
This is actually stored in duckboard and the primary, canonical source of truth.
duckboard may, accoring to configuration, decide to collapse multiple deltas in
the past into a single one to reduce storage space and make queries more
efficient. This process is called rarification and may e.g. keep all deltas for
the last week, but only one per 24h before that.

To make retrieval quicker, duckboard will store snapshots of the aggregated
deltas for some points in time, most notably the time of the most recent delta.
This way the number of records that need to be aggregated is kept bound, and
kept to 1 for the most common use case of querying the current state. This is
considered secondary, non-canonical information as it can always be rebuild from
the primary.

### Timestamps

Timestamps need to be guaranteed unique within a duckboard cluster, to allow the
asyncronous replication to work. We use a fairly boring scheme stolen from
twitter's snowflake, but with diffferent tuning:
- 43 bits of milliseconds since 2019-01-01T00:00:00Z
- 8 bits of node id
- 12 bits of a sequence counter, protected against rollover in the same ms
 
### Merkle Trees

In order to replicate changes between two clusters, duckboard will maintain a
merkle tree over the changes in each scope. To do this all changes in the scope
are conceptually ordered by timestamp (which is unique), and then grouped into
UTC days. Within each day-group the timestamps are added to an empty hash in
order, yielding the hash for this day. A timeline is then constructed by adding
these day-hashes to an empty hash in order again. This way we can quickly figure
out which of the recent days have not been fully replicated, and for each of
them which changes are not the same on both sides. The missing changes can then
be transferred, making this process converge.

XXX this is not needed in the first version

### Caching

Each duckboard node can employ caching at two different levels to speed up
operations: within the local database certain data can be persisted in a
redundant fashion, e.g. snapshots of item states in certain intervals, or merkle
hashes for a certain point in time. This is safe as long as we can make all
writes to the local database keep these accurately and atomically up to date,
and can make many operations that would be O(N) bound to a certain upper size.

On top of this, each node can use caching in memory of e.g. a compact
representation of the current item states to reduce the number of calls that
need to go to the DB in the first place.

Execution
---------

### 0.0.1 First Items

* REST-only, no websockets or other notifications
* PUT/GET/DEL for individual items with properties
* GET/POST on collection of items
* memory-only store
* timestamp generation
* ?at=<timestamp> on GETs to retrieve state at specific time

### 0.0.2 Filtering

* GETs to support filtering grammar
* Archiving
