#!/usr/bin/env swift
import CoreBluetooth
import Foundation

final class LunaSayScanner: NSObject, CBCentralManagerDelegate {
    private var central: CBCentralManager!
    private let needle: String
    private let timeout: TimeInterval
    private var finished = false

    init(needle: String, timeout: TimeInterval) {
        self.needle = needle.lowercased()
        self.timeout = timeout
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
        DispatchQueue.main.asyncAfter(deadline: .now() + timeout) { [weak self] in
            self?.finish(code: 2, payload: ["found": false, "error": "timeout"])
        }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard !finished else { return }
        guard central.state == .poweredOn else {
            if central.state == .unsupported || central.state == .unauthorized || central.state == .poweredOff {
                finish(code: 3, payload: ["found": false, "error": "bluetooth-state-\(central.state.rawValue)"])
            }
            return
        }
        central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        guard !finished else { return }
        let advertised = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        let name = advertised ?? peripheral.name ?? ""
        guard name.lowercased().contains(needle) else { return }
        finish(code: 0, payload: [
            "found": true,
            "name": name,
            "identifier": peripheral.identifier.uuidString,
            "rssi": RSSI.intValue,
        ])
    }

    private func finish(code: Int32, payload: [String: Any]) {
        guard !finished else { return }
        finished = true
        central?.stopScan()
        if let data = try? JSONSerialization.data(withJSONObject: payload, options: [.sortedKeys]),
           let line = String(data: data, encoding: .utf8) {
            print(line)
        }
        fflush(stdout)
        exit(code)
    }
}

var needle = "Astrolabe"
var timeout: TimeInterval = 6
var index = 1
while index < CommandLine.arguments.count {
    switch CommandLine.arguments[index] {
    case "--name-contains" where index + 1 < CommandLine.arguments.count:
        index += 1
        needle = CommandLine.arguments[index]
    case "--timeout" where index + 1 < CommandLine.arguments.count:
        index += 1
        timeout = Double(CommandLine.arguments[index]) ?? timeout
    case "--help":
        print("usage: lunasay_ble_probe [--name-contains TEXT] [--timeout SECONDS]")
        exit(0)
    default:
        fputs("unknown argument: \(CommandLine.arguments[index])\n", stderr)
        exit(64)
    }
    index += 1
}

_ = LunaSayScanner(needle: needle, timeout: max(1, timeout))
RunLoop.main.run()
