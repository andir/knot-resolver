.. _mod-http:

HTTP/2 services
---------------

This is a module that does the heavy lifting to provide an HTTP/2 enabled
server that supports TLS by default and provides endpoint for other modules
in order to enable them to export restful APIs and websocket streams.
One example is statistics module that can stream live metrics on the website,
or publish metrics on request for Prometheus scraper.

The server allows other modules to either use default endpoint that provides
built-in webpage, restful APIs and websocket streams, or create new endpoints.

Example configuration
^^^^^^^^^^^^^^^^^^^^^

By default, the web interface starts HTTPS/2 on port 8053 using an ephemeral
certificate that is valid for 90 days and is automatically renewed. It is of
course self-signed, so you should use your own judgement before exposing it
to the outside world. Why not use something like `Let's Encrypt <https://letsencrypt.org>`_
for starters?

.. code-block:: lua

	-- Load HTTP module with defaults
	modules = {
		http = {
			host = 'localhost',
			port = 8053,
		}
	}

Now you can reach the web services and APIs, done!

.. code-block:: bash

	$ curl -k https://localhost:8053
	$ curl -k https://localhost:8053/stats

It is possible to disable HTTPS altogether by passing ``cert = false`` option.
While it's not recommended, it could be fine for localhost tests as, for example,
Safari doesn't allow WebSockets over HTTPS with a self-signed certificate.
Major drawback is that current browsers won't do HTTP/2 over insecure connection.

.. code-block:: lua

	http = {
		host = 'localhost',
		port = 8053,
		cert = false,
	}

If you want to provide your own certificate and key, you're welcome to do so:

.. code-block:: lua

	http = {
		host = 'localhost',
		port = 8053,
		cert = 'mycert.crt',
		key  = 'mykey.key',
	}

The format of both certificate and key is expected to be PEM, e.g. equivallent to
the outputs of following: 

.. code-block:: bash

	openssl ecparam -genkey -name prime256v1 -out mykey.key
	openssl req -new -key mykey.key -out csr.pem
	openssl req -x509 -days 90 -key mykey.key -in csr.pem -out mycert.crt

How to expose services over HTTP
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The module provides a table ``endpoints`` of already existing endpoints, it is free for reading and
writing. It contains tables describing a triplet - ``{mime, on_serve, on_websocket}``.
In order to register a new service, simply add it to the table:

.. code-block:: lua

	http.endpoints['/health'] = {'application/json',
	function (h, stream)
		-- API call, return a JSON table
		return {state = 'up', uptime = 0}
	end,
	function (h, ws)
		-- Stream current status every second
		local ok = true
		while ok do
			local push = tojson('up')
			ok = ws:send(tojson({'up'}))
			require('cqueues').sleep(1)
		end
		-- Finalize the WebSocket
		ws:close()
	end}

Then you can query the API endpoint, or tail the WebSocket using curl.

.. code-block:: bash

	$ curl -k http://localhost:8053/health
	{"state":"up","uptime":0}
	$ curl -k -i -N -H "Connection: Upgrade" -H "Upgrade: websocket" -H "Host: localhost:8053/health"  -H "Sec-Websocket-Key: nope" -H "Sec-Websocket-Version: 13" https://localhost:8053/health
	HTTP/1.1 101 Switching Protocols
	upgrade: websocket
	sec-websocket-accept: eg18mwU7CDRGUF1Q+EJwPM335eM=
	connection: upgrade

	?["up"]?["up"]?["up"]

Since the stream handlers are effectively coroutines, you are free to keep state and yield using cqueues.
This is especially useful for WebSockets, as you can stream content in a simple loop instead of
chains of callbacks.

Last thing you can publish from modules are *"snippets"*. Snippets are plain pieces of HTML code that are rendered at the end of the built-in webpage. The snippets can be extended with JS code to talk to already
exported restful APIs and subscribe to WebSockets.

.. code-block:: lua

	http.snippets['/health'] = {'Health service', '<p>UP!</p>'}

How to expose more interfaces
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Services exposed in the previous part share the same external interface. This means that it's either accessible to the outside world or internally, but not one or another. This is not always desired, i.e. you might want to offer DNS/HTTPS to everyone, but allow application firewall configuration only on localhost. ``http`` module allows you to create additional interfaces with custom endpoints for this purpose.

.. code-block:: lua

	http.interface('127.0.0.1', 8080, {
		['/conf'] = {'application/json', function (h, stream) print('configuration API') end},
		['/private'] = {'text/html', static_page},
	})

This way you can have different internal-facing and external-facing services at the same time.

Dependencies
^^^^^^^^^^^^

* `lua-http <https://github.com/daurnimator/lua-http>`_ available in LuaRocks

    ``$ luarocks install --server=http://luarocks.org/dev http``