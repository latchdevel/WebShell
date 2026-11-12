/*
   WebShell: Web-based Command Line Interface (CLI) library for Arduino

   This library enables remote CLI access through a web browser using bidirectional
   communication. It bridges the standard Arduino Stream interface with WebSocket
   connections, allowing a "Shell" command parser to operate directly over the web.

   It leverages the "Shell" library to provide a command-line interface via a web
   browser, using the "mWebSockets" library to manage the WebSocket connections.

   See: 
      - https://github.com/latchdevel/Shell
        Provides the underlying CLI command parsing and execution engine.

      - https://github.com/latchdevel/mWebSockets
        Handles the low-level WebSocket server and bidirectional data framing.

   Implements two core classes:
      - WebSocketStream: A wrapper class that encapsulates a WebSocket connection 
                         so that it behaves like a standard Arduino Stream object.
      - WebShellManager: Manages the lifecycle and execution of WebShell sessions.

   Copyright (c) 2026 Jorge Rivera. https://github.com/latchdevel/WebShell
   License GNU Lesser General Public License v3.0.
*/

#ifndef WEBSHELL_H_INCLUDED
#define WEBSHELL_H_INCLUDED

#include <Arduino.h>

// Forward declarations to minimize dependencies in the header
class WebSocketStream;
class Shell;
class ShellArguments;
namespace net { class WebSocket; }

typedef void (*ShellCommandFunc)(Shell &shell, int argc, const ShellArguments &argv);

// --------------------------------------- WebSocket Stream -------------------------------------------- //

/**
 * @brief Adapter class that wraps a net::WebSocket to behave like a standard Stream.
 *        It uses an internal ring buffer to store incoming websocket data.
 */
class WebSocketStream : public Stream {
   private:
      // Maximum capacity of the internal ring buffer
      static constexpr uint8_t WS_RX_BUFFER_SIZE = 16;

      // Pointer to the underlying WebSocket instance
      net::WebSocket* m_ws{nullptr};

      // Static Ring Buffer members
      uint8_t m_rxBuffer[WS_RX_BUFFER_SIZE]{};  // Internal buffer array
      volatile uint8_t m_head{0};               // Index where the WebSocket writes new data
      volatile uint8_t m_tail{0};               // Index where the Stream reads data from

   public:
      /**
       * @brief Default constructor. Initializes the stream in an unattached state.
       */
      explicit WebSocketStream();

      /**
       * @brief Virtual destructor. Ensures proper cleanup of derived resources.
       */
      virtual ~WebSocketStream() = default; 

      /**
       * @brief Attaches a live WebSocket instance to this stream adapter.
       * @param ws Reference to the active net::WebSocket.
       */
      void attachSocket(net::WebSocket& ws);

      /**
       * @brief Detaches the current WebSocket and resets the ring buffer indices.
       */
      void detachSocket();

      /**
       * @brief Pushes incoming network data into the internal ring buffer.
       *        This should be triggered by the WebSocket's receive event.
       * @param data Pointer to the incoming byte array.
       * @param length Number of bytes to push.
       */
      void pushData(const uint8_t* data, uint8_t length);

      // --- MANDATORY ARDUINO STREAM METHODS --- //

      /**
       * @brief Returns the number of bytes available for reading in the ring buffer.
       * @return The count of readable bytes.
       */
      int available() override;

      /**
       * @brief Reads the next available byte from the ring buffer.
       * @return The byte value (0-255), or -1 if the buffer is empty.
       */
      int read() override;

      /**
       * @brief Peeks at the next available byte without removing it from the buffer.
       * @return The byte value (0-255), or -1 if the buffer is empty.
       */
      int peek() override;

      // --- MANDATORY ARDUINO PRINT METHODS --- //

      /**
       * @brief Writes a single byte to the underlying WebSocket.
       * @param byte The byte to transmit.
       * @return size_t The number of bytes written (1 on success, 0 on failure).
       */
      size_t write(uint8_t byte) override;

      /**
       * @brief Writes a buffer of data to the underlying WebSocket as a single transmission block.
       * @param buffer Pointer to the byte array data.
       * @param size The number of bytes to transmit.
       * @return size_t The total number of bytes written.
       */
      size_t write(const uint8_t* buffer, size_t size) override;

      /**
       * @brief Flushes any buffered outgoing data. 
       *        Left empty as transmission is handled synchronously.
       */
      void flush() override;

};

// --------------------------------------- WebShell Manager -------------------------------------------- //

/**
 * @brief Manages and coordinates multiple concurrent WebShell sessions.
 */
class WebShellManager {
   private:
      /**
       * @brief Internal container grouping the lifecycle components of a single active session.
       */
      struct Session {
         net::WebSocket*   socket{nullptr};  // Pointer to the underlying active network socket
         WebSocketStream*  stream{nullptr};  // Pointer to the stream adapter associated with this session
         Shell*            shell{nullptr};   // Pointer to the shell execution instance
         uint32_t          startTime{0};     // Timestamp (millis) when the session was established
      };

      /**
       * @brief Pointer to the dynamically allocated array of session pool.
       */
      Session* m_sessions;

      /**
       * @brief Internal helper function to safely deallocate a session's resources and prevent memory leaks.
       * @param index The static array index representing the target session slot to be cleared.
       */
      void releaseSessionSlot(uint8_t index);

   public:
      /**
       * @brief Maximum number of web shell sessions allowed.
       *        Set during object construction to determine the fixed size of the session pool.
       */
      const uint8_t maxWebShellSessions;

      /**
       * @brief Constructor. Initializes the manager with a fixed session limit set at instantiation. 
       * @param maxWebShellSessions The maximum number of concurrent sessions allowed for this instance.
       */
      explicit WebShellManager(uint8_t maxWebShellSessions);

      /**
       * @brief Destructor. Ensures thorough cleanup of all active sessions to prevent memory leaks.
       */
      ~WebShellManager();

      /**
       * @brief Event handler triggered when a new client establishes a connection.
       *        Locates an empty resource slot, instantiates dependencies, and initializes the shell.
       * @param ws Reference to the newly connected net::WebSocket instance.
       * @param maxHistory Optional number of commands to allocate in the history stack for scrolling 
       *                   back through using Up/Down arrow keys.
       */
      void onConnect(net::WebSocket& ws, size_t maxHistory = 0);

      /**
       * @brief Event handler triggered when a client disconnects from the network.
       *        Locates the session slot tied to the given WebSocket and releases its resources.
       * @param ws Reference to the disconnected net::WebSocket instance.
       */
      void onDisconnect(net::WebSocket& ws);

      /**
       * @brief Event handler triggered when incoming data is received from a WebSocket client.
       *        Routes the payload to the matching session's internal stream buffer.
       * @param ws Reference to the active net::WebSocket instance sending the data.
       * @param data Pointer to the incoming character payload buffer.
       * @param length Size of the incoming data payload.
       */
      void onData(net::WebSocket& ws, const char* data, size_t length);

      /**
       * @brief Periodically updates active shell sessions and terminates expired ones.
       * 
       * Iterates through all connection slots, executes the internal loop for active 
       * shells, and checks if any session has exceeded the maximum idle timeout.
       */
      void loop();

      /**
       * @brief Closes all active WebShell sessions.
       *        Iterates through all connection slots and gracefully closes any active
       *        WebSocket connections using a normal closure code.
       */
      void closeAll();

      /**
       * @brief Closes a specific WebShell session based on its WebSocket pointer.
       * @param ws Pointer to the target net::WebSocket connection to be closed.
       */
      void close(net::WebSocket* ws);

      /**
       * @brief Retrieves the WebSocket pointer for a given session index.
       * @param index The session index to query must be less than maxWebShellSessions.
       * @return Pointer to the net::WebSocket if active and valid, nullptr otherwise.
       */
      net::WebSocket* getSessionSocket(uint8_t index) const;

      /**
       * @brief Counts how many WebShell sessions are currently active.
       * @return uint8_t The number of active WebShell sessions.
       */
      uint8_t getActiveSessionsCount() const;

      /**
       * @brief Finds the WebSocket associated with a specific Shell.
       * @param shell Reference to the Shell instance to search for.
       * @return net::WebSocket* Pointer to the socket, or nullptr if not found.
       */
      net::WebSocket* getSocketByShell(const Shell& shell) const;

      /**
       * @brief Finds the Shell associated with a specific WebSocket.
       * @param socket Pointer to the WebSocket instance to search for.
       * @return Shell* Pointer to the Shell, or nullptr if not found.
       */
      Shell* getShellBySocket(const net::WebSocket* socket) const;

      /**
       * @brief Gets the active session duration in seconds, searching by its WebSocket pointer.
       * @param ws Pointer to the WebSocket session.
       * @return uint32_t Elapsed time in seconds. Returns 0 if the session does not exist.
       */
      uint32_t getSessionAgeInSeconds(net::WebSocket* ws) const;

};

#endif // WEBSHELL_H_INCLUDED
