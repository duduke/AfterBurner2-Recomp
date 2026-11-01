// Rebuild the original, ROM-free TV launcher banner on macOS.
import AppKit
let image = NSImage(size: NSSize(width: 320, height: 180))
image.lockFocus()
NSColor(red: 0.063, green: 0.11, blue: 0.188, alpha: 1).setFill()
NSBezierPath(rect: NSRect(x: 0, y: 0, width: 320, height: 180)).fill()
let plane = NSBezierPath()
let points: [(Double, Double)] = [(160,166),(168,129),(205,100),(169,105),(165,77),(160,83),(155,77),(151,105),(115,100),(152,129)]
plane.move(to: NSPoint(x: points[0].0, y: points[0].1))
for p in points.dropFirst() { plane.line(to: NSPoint(x: p.0, y: p.1)) }
plane.close()
NSColor(red: 0.33, green: 0.78, blue: 1, alpha: 1).setFill()
plane.fill()
func text(_ value: String, _ size: CGFloat, _ y: CGFloat, _ color: NSColor) {
    let attributes: [NSAttributedString.Key: Any] = [.font: NSFont.boldSystemFont(ofSize: size), .foregroundColor: color]
    let width = (value as NSString).size(withAttributes: attributes).width
    (value as NSString).draw(at: NSPoint(x: (320-width)/2, y: y), withAttributes: attributes)
}
text("AFTER BURNER II", 25, 39, .white)
text("RECOMP · ANDROID PREVIEW", 10, 17, NSColor(red: 0.53, green: 0.64, blue: 0.74, alpha: 1))
image.unlockFocus()
let bitmap = NSBitmapImageRep(data: image.tiffRepresentation!)!
try bitmap.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: "android/app/src/main/res/drawable/banner.png"))
