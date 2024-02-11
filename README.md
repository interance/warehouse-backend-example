This example illustrates a simple backend service for a warehouse.

The service has a SQLite database for storing the warehouse inventory. The
service provides a REST API for managing the inventory. If configured, the
service also starts a trivial TCP interface that allows to increment and
decrement the available inventory on an item. The TCP interface simply receives
line-separated JSON.

Each item has a unique identifier, a name, a price, and a quantity.

The service also provides a WebSocket endpoint that allows clients to receive a
stream of updates to the inventory. Whenever the available quantity of an item
changes, the service sends a JSON message to all connected WebSocket clients.

The implementation is separated into the following C++ types:

- `database`: A simple SQLite database wrapper.
- `database_actor`: A typed actor that provides a high-level API for managing
  the inventory in the database. It holds a pointer to a `database` object and
  dispatches requests to it.
- `ec`: A simple error code type.
- `http_server`: Provides callbacks for handling HTTP requests. It translates
  between HTTP requests from clients and messages to the `database_actor`.
- `item`: A simple data type that represents an item in the warehouse.
