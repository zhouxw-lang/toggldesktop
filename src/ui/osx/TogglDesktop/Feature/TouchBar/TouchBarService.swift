//
//  TouchBarService.swift
//  TogglDesktop
//
//  Created by Nghia Tran on 10/1/19.
//  Copyright © 2019 Alari. All rights reserved.
//

import Foundation

@objc protocol TouchBarServiceDelegate: class {

    func touchBarServiceStartTimeEntryOnTap()
}

@available(OSX 10.12.2, *)
@objcMembers
final class TouchBarService: NSObject {

    static let shared = TouchBarService()

    enum DisplayState {
        case tracking
        case normal
    }

    fileprivate struct Constants {
        static let TimeEntryIdentifer = NSUserInterfaceItemIdentifier("TimeEntryScrubberItem")
        static let NumberTimeEntry = 10
    }

    // MARK: OUTLET

    private lazy var startButton: NSButton = {
        let btn = NSButton(title: "Start", target: self, action: #selector(self.startBtnOnTap(_:)))
        btn.image = NSImage(named: NSImage.touchBarPlayTemplateName)
        btn.alternateImage = NSImage(named: NSImage.touchBarRecordStopTemplateName)
        btn.imagePosition = .imageOnly
        btn.setButtonType(.toggle)
        return btn
    }()

    private lazy var runningTimeEntryBtn = NSButton(title: "Running...", target: nil, action: nil)

    // MARK: Variables

    var isEnabled = true
    private var isPresented = false
    private lazy var touchBar: NSTouchBar = {
        let touchBar = NSTouchBar()
        touchBar.delegate = self
        touchBar.customizationIdentifier = .mainTouchBar
        var items: [NSTouchBarItem.Identifier] = [.timeEntryItem, .startStopItem]

        // Add Esc button since Touch Bar (PrivateAPI) will override the system ESC key
        // Based on https://github.com/pigigaldi/Pock/blob/master/Pock/TouchBar/PockMainController/PockMainController.swift#L57
        #if !APP_STORE
        items.insert(.escButtonItem, at: 0)
        #endif

        // Set all touch bar buttons
        touchBar.defaultItemIdentifiers = items
        return touchBar
    }()

    weak var delegate: TouchBarServiceDelegate?
    private var timeEntries: [TimeEntryViewItem] = []
    private var displayState = DisplayState.normal { didSet { updateDisplayState() }}
    private lazy var scrubberView: NSScrubber = {
        let view = NSScrubber(frame: .zero)
        view.delegate = self
        view.dataSource = self
        view.mode = .free
        view.selectionBackgroundStyle = .outlineOverlay
        view.selectionOverlayStyle = .outlineOverlay
        view.showsAdditionalContentIndicators = true
        view.register(NSNib(nibNamed: NSNib.Name("TimeEntryScrubberItem"), bundle: nil), forItemIdentifier: Constants.TimeEntryIdentifer)
        let layout = TimeEntryScrubberFlowLayout()
        layout.itemSpacing = 8
        layout.delegate = self
        view.scrubberLayout = layout
        return view
    }()

    // MARK: Init

    override init() {
        super.init()
        initCommon()
        initNotification()
    }

    // MARK: Public

    func makeTouchBar() -> NSTouchBar? {
        guard isEnabled else { return nil }

        // Use public APIs for AppStore version
        #if APP_STORE
            return touchBar
        #endif

        // Return nil and use Private APIs
        return nil
    }

    func updateRunningItem(_ timeEntry: TimeEntryViewItem) {
        runningTimeEntryBtn.title = timeEntry.touchBarTitle
        if let colorStr = timeEntry.projectColor,
            !colorStr.isEmpty,
            let color = ConvertHexColor.hexCode(toNSColor: colorStr) {
            runningTimeEntryBtn.bezelColor = color
        } else {
            runningTimeEntryBtn.bezelColor = nil
        }
    }

    func updateTimeEntryList(_ timeEntries: [TimeEntryViewItem]) {
        // Only get unique Time Entry (Don't get the TE in the group)
        var touchBarEntries: [TimeEntryViewItem] = []
        for timeEntry in timeEntries {
            // Ingore the time entry in the group
            if timeEntry.groupOpen && !timeEntry.group {
                continue
            }

            // Only add unique TE
            if touchBarEntries.first(where: { $0.isSameContent(with: timeEntry)}) == nil {
                touchBarEntries.append(timeEntry)
            }

            if touchBarEntries.count >= Constants.NumberTimeEntry {
                break
            }
        }
        self.timeEntries = (touchBarEntries)
        scrubberView.reloadData()
    }

    func resetContent() {
        self.timeEntries = []
        scrubberView.reloadData()
        startButton.isHidden = true
    }

    func prepareContent() {
        scrubberView.reloadData()
        startButton.isHidden = false
    }
}

// MARK: Action

@available(OSX 10.12.2, *)
extension TouchBarService {

    func present() {
        #if !APP_STORE
        guard isEnabled && !isPresented else { return }
        if NSTouchBar.presentSystemModal(touchBar, systemTrayItemIdentifier: nil) {
            isPresented = true
        }
        #endif
    }

    func minimize() {
        #if !APP_STORE
        guard isEnabled && isPresented else { return }
        NSTouchBar.minimizeSystemModal(touchBar)
        isPresented = false
        #endif
    }

    func dismiss() {
        #if !APP_STORE
        guard isEnabled && isPresented else { return }
        NSTouchBar.dismissSystemModal(touchBar)
        isPresented = false
        #endif
    }
}

// MARK: Private

@available(OSX 10.12.2, *)
extension TouchBarService {

    fileprivate func initCommon() {
        NSApplication.shared.isAutomaticCustomizeTouchBarMenuItemEnabled = false
    }

    fileprivate func initNotification() {
        NotificationCenter.default.addObserver(self,
                                               selector: #selector(self.stateButtonTimerBarChangeNotification(_:)),
                                               name: NSNotification.Name(kStartButtonStateChange),
                                               object: nil)
    }

    @objc private func stateButtonTimerBarChangeNotification(_ noti: Notification) {
        guard let value = noti.object as? NSNumber else {
            return
        }
        startButton.state = NSControl.StateValue(rawValue: value.intValue)
        displayState = startButton.state == .on ? .tracking : .normal
    }

    private func updateDisplayState() {
        switch displayState {
        case .normal:
            touchBar.defaultItemIdentifiers.removeAll(where: { $0 == .runningTimeEntry })
        case .tracking:
            let count = touchBar.defaultItemIdentifiers.count
            touchBar.defaultItemIdentifiers.insert(.runningTimeEntry, at: count - 1)
        }
    }
}

// MARK: NSTouchBarDelegate

@available(OSX 10.12.2, *)
extension TouchBarService: NSTouchBarDelegate {

    func touchBar(_ touchBar: NSTouchBar, makeItemForIdentifier identifier: NSTouchBarItem.Identifier) -> NSTouchBarItem? {
        switch identifier {
        case NSTouchBarItem.Identifier.timeEntryItem:
            let item = NSCustomTouchBarItem(identifier: identifier)
            item.view = scrubberView
            return item
        case NSTouchBarItem.Identifier.runningTimeEntry:
            let item = NSCustomTouchBarItem(identifier: identifier)
            item.visibilityPriority = .high
            item.view = runningTimeEntryBtn
            return item
        case NSTouchBarItem.Identifier.startStopItem:
            let item = NSCustomTouchBarItem(identifier: identifier)
            item.visibilityPriority = .high
            item.view = startButton
            return item
        case .escButtonItem:
            let item = NSCustomTouchBarItem(identifier: identifier)
            item.visibilityPriority = .high
            item.view = EscButton()
            return item
        default:
            return nil
        }
    }
}

// MARK: Private

@available(OSX 10.12.2, *)
extension TouchBarService {

    @objc fileprivate func startBtnOnTap(_ sender: NSButton) {
        delegate?.touchBarServiceStartTimeEntryOnTap()
    }
}

// MARK: NSScrubberDataSource

@available(OSX 10.12.2, *)
extension TouchBarService: NSScrubberDelegate, NSScrubberDataSource {

    func numberOfItems(for scrubber: NSScrubber) -> Int {
        return timeEntries.count
    }

    func scrubber(_ scrubber: NSScrubber, viewForItemAt index: Int) -> NSScrubberItemView {
        let timeEntry = timeEntries[index]
        guard let cell = scrubber.makeItem(withIdentifier: Constants.TimeEntryIdentifer, owner: self) as? TimeEntryScrubberItem
            else { return NSScrubberItemView() }
        cell.config(timeEntry)
        return cell
    }

    func scrubber(_ scrubber: NSScrubber, didSelectItemAt selectedIndex: Int) {
        continueTimeEntry(at: selectedIndex)
    }

    private func continueTimeEntry(at index: Int) {
        guard let item = timeEntries[safe: index] else { return }
        NotificationCenter.default.post(name: NSNotification.Name(kCommandContinue), object: item.guid)

        // Deselect, so we can select it again
        scrubberView.selectedIndex = -1
    }
}

// MARK: TimeEntryScrubberFlowLayoutDelegate

@available(OSX 10.12.2, *)
extension TouchBarService: TimeEntryScrubberFlowLayoutDelegate {

    func timeEntryScrubberTitle(at index: Int) -> String {
        let timeEntry = timeEntries[index]
        return timeEntry.touchBarTitle
    }
}
