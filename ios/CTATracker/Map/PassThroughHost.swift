import SwiftUI
import UIKit

/// Hosts SwiftUI content over the map and hands every touch above `openAbove()` (screen y) to the views below,
/// so the open map above the card list keeps panning (redesign spec 2). A SwiftUI ScrollView alone would take
/// every touch on its frame, even over its empty top margin.
///
/// The content is in its own hosting controller, so SwiftUI environment values don't reach it: pass them in.
struct PassThroughHost<Content: View>: UIViewControllerRepresentable {
    /// Read at touch time, not in the caller's body: the list's top moves every scroll frame, and reading it there
    /// rebuilt the whole map each frame.
    let openAbove: () -> CGFloat
    @ViewBuilder let content: Content

    func makeUIViewController(context: Context) -> Controller { Controller(rootView: content) }

    func updateUIViewController(_ controller: Controller, context: Context) {
        controller.host.rootView = content
        controller.pass.openAbove = openAbove
    }

    final class Controller: UIViewController {
        let host: UIHostingController<Content>
        let pass = PassView()

        init(rootView: Content) {
            host = UIHostingController(rootView: rootView)
            super.init(nibName: nil, bundle: nil)
        }

        @available(*, unavailable) required init?(coder: NSCoder) { fatalError("not used") }

        override func loadView() { view = pass }

        override func viewDidLoad() {
            super.viewDidLoad()
            view.backgroundColor = .clear
            addChild(host)
            host.view.backgroundColor = .clear
            host.view.frame = view.bounds
            host.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
            view.addSubview(host.view)
            host.didMove(toParent: self)
        }
    }

    final class PassView: UIView {
        var openAbove: () -> CGFloat = { 0 }

        override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
            // In window coordinates, so it matches the content's `.global` frames whatever this view's origin.
            let y = convert(point, to: nil).y
            return y < openAbove() ? nil : super.hitTest(point, with: event)
        }
    }
}
