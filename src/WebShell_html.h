#ifndef WEBSHELL_INDEX_HTML_H
#define WEBSHELL_INDEX_HTML_H

#include <Arduino.h>

const char indexHtml[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WebShell</title>
    <link rel="icon" type="image/png" href="data:image/png;base64,AAABAAEAEBAAAAEAIABoBAAAFgAAACgAAAAQAAAAIAAAAAEAIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD7+///+/v///v7///7+///+/v///v7///7+///+/v///v7///7+///+/v///v7///7+///+/v///v7///7+///+/v//wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/+/v///v7//8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA//v7///7+///AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP/7+///+/v//wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/+/v///v7//8AAAD/AP8A/wD/AP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA//v7///7+///AAAA/wD/AP8A/wD/AP8A/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP/7+///+/v//wAAAP8AAAD/AP8A/wD/AP8A/wD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/+/v///v7//8AAAD/AAAA/wAAAP8A/wD/AP8A/wD/AP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA//v7///5+f//AAAA/wAAAP8AAAD/AAAA/wD/AP8A/wD/AP8A/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP/7+///+/v//wAAAP8AAAD/AAAA/wD/AP8A/wD/AP8A/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/+/v///v7//8AAAD/AAAA/wD/AP8A/wD/AP8A/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA//v7///7+///AAAA/wD/AP8A/wD/AP8A/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP/7+///+/v//wAAAP8A/wD/AP8A/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/+/v///v7//8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA/wAAAP8AAAD/AAAA//v7///7+///+/v///v7///7+///+/v///v7///7+///+/v///v7///7+///+/v///v7///7+///+/v///v7///7+///AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==">
    <link href="https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.min.css" rel="stylesheet">
    <style>
        html, body { margin: 0; padding: 0; width: 100%; height: 100%; background-color: #000000; overflow: hidden; }
        #terminal-container { width: 100%; height: 100%; padding: 10px; box-sizing: border-box; }
    </style>
</head>
<body>
    <div id="terminal-container"></div>
    <script src="https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/xterm-addon-fit@0.8.0/lib/xterm-addon-fit.min.js"></script>
    <script>
        let socket;
        const term = new Terminal({ cursorBlink: true, cursorStyle: 'underline', fontSize: 16, fontFamily: 'Courier New, monospace', theme: { background: '#000000', foreground: '#00FF00' } });
        const fitAddon = new FitAddon.FitAddon();
        term.loadAddon(fitAddon);
        term.open(document.getElementById('terminal-container'));
        fitAddon.fit();
        function connectWebSocket() {
            if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) { return; }
            term.writeln("Connecting... ");
            socket = new WebSocket(`ws://${window.location.hostname}:81/`);
            socket.onopen = () => { term.writeln("Connected successfully!\r\n"); };
            socket.onmessage = async (event) => { const text = await event.data.text(); term.write(text); };
            socket.onclose = () => { term.writeln("\r\nDisconnected! Press any key to reconnect..."); };
            socket.onerror = () => { term.writeln("\r\nERROR: WebSocket unavailable!"); };
        }
        term.onData(data => {
            if (socket && socket.readyState === WebSocket.OPEN) { 
                socket.send(data);
            } else { 
                connectWebSocket();
            }
        });
        window.addEventListener('resize', () => { fitAddon.fit(); });
        window.addEventListener('beforeunload', () => { if (socket && socket.readyState === WebSocket.OPEN) { socket.close(); } });
        connectWebSocket();
    </script>
</body>
</html>
)html";

#endif // WEBSHELL_INDEX_HTML_H
