const net = require('net');
net.createServer({allowHalfOpen: true}, (socket) => {
    socket.on('data', (buffer) => {
        console.log(buffer);
        socket.write(buffer);
    });
    socket.on('close', () => {
        console.log('close');
    })
}).listen(8000);