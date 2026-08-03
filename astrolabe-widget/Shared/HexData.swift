import Foundation

extension Data {
    init?(hex: String) {
        let clean = hex.trimmingCharacters(in: .whitespacesAndNewlines)
        guard clean.count.isMultiple(of: 2) else { return nil }
        var data = Data()
        var index = clean.startIndex
        while index < clean.endIndex {
            let next = clean.index(index, offsetBy: 2)
            guard let byte = UInt8(clean[index..<next], radix: 16) else { return nil }
            data.append(byte)
            index = next
        }
        self = data
    }
}
