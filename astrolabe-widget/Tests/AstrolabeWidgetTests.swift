import XCTest

final class AstrolabeWidgetTests: XCTestCase {
    func testHexDataParsing() {
        XCTAssertEqual(Data(hex: "00a1FF"), Data([0x00, 0xa1, 0xff]))
        XCTAssertNil(Data(hex: "xyz"))
    }
}
