var ap = require('bindings')('usbspy.node');
var EventEmitter = require('events');

class AsyncProg extends EventEmitter {

}

var asyncProgEE = new AsyncProg();


asyncProgEE.spyOn = () => {
    ap.spyOn((data) => {
        asyncProgEE.emit('change', data);
    }, (data) => {
        asyncProgEE.emit('end', data);
    });
}

asyncProgEE.spyOff = () => {
    ap.spyOff();
}

asyncProgEE.getAvailableUSBDevices = () => {
    return ap.getAvailableUSBDevices();
};

asyncProgEE.getUSBDeviceByDeviceLetter = (letter) => {
    return ap.getUSBDeviceByDeviceLetter(letter);
};

module.exports = asyncProgEE;