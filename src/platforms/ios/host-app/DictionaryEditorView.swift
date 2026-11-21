/*
 * DictionaryEditorView -- Dictionary editor with type and language selection.
 *
 * Supports three dictionary sub-types (pronounce, stress, compound)
 * loaded from the C++ data query API. Each type has type-specific
 * add/edit dialogs. Paginated loading (100 entries at a time).
 *
 * License: GPL-3.0
 */

import SwiftUI
import UniformTypeIdentifiers

/// Human-readable label for a dict type.
private func dictTypeLabel(_ type: String) -> String {
    switch type {
    case "pronounce": return "Pronunciation"
    case "user":      return "Pronunciation (user)"
    case "compound":  return "Compound"
    case "stress":    return "Stress"
    case "character": return "Characters"
    default:          return type.prefix(1).uppercased() + type.dropFirst()
    }
}

/// The backend dict type. "user" is a client-side filter on "pronounce".
private func backendDictType(_ type: String) -> String { type == "user" ? "pronounce" : type }

/// True if the type uses the pronounce format (5-column TSV with IPA).
private func isPronounceType(_ type: String) -> Bool { type == "pronounce" || type == "user" }

// MARK: - Dictionary Editor

struct DictionaryEditorView: View {
    @ObservedObject var engine: TgsbEngine
    @Binding var engineStarted: Bool

    @State private var selectedType: String = ""
    @State private var langFilter: String = ""
    @State private var searchQuery: String = ""
    @State private var activeSearch: String = ""
    @State private var loadedCount: Int = 0
    @State private var showAddSheet = false
    @State private var editingEntry: TgsbEngine.DictEntry? = nil
    @State private var showRemoveConfirm = false
    @State private var showExcludeSheet = false
    @State private var showExcludeCategoriesSheet = false
    @State private var statusMessage: String?

    // Export state — single fileExporter to avoid SwiftUI dual-modifier conflict
    @State private var showExportPicker = false
    @State private var exportUserOnly = false
    @State private var showImportPicker = false

    /// Dict types with synthetic "user" injected after "pronounce".
    private var displayTypes: [TgsbEngine.DictType] {
        var result = engine.dictTypes
        if let idx = result.firstIndex(where: { $0.type == "pronounce" }) {
            result.insert(TgsbEngine.DictType(id: "user", type: "user", count: 0), at: result.index(after: idx))
        }
        return result
    }

    /// When "user" is selected, filter to user-only entries.
    private var filteredEntries: [TgsbEngine.DictEntry] {
        if selectedType == "user" {
            return engine.dictionaryEntries.filter { $0.source == "user" }
        }
        return engine.dictionaryEntries
    }

    private var typePickerLabel: String {
        if selectedType.isEmpty { return "Select type" }
        return "\(dictTypeLabel(selectedType)) (\(filteredEntries.count))"
    }

    private var previewClosure: ((String, String, String) -> Void)? {
        (isPronounceType(selectedType) || selectedType == "character")
            ? { from, to, tIpa in engine.previewDictEntry(from: from, to: to, toIpa: tIpa) }
            : nil
    }
    private var textToIpaClosure: ((String) -> String)? {
        isPronounceType(selectedType) ? { text in engine.textToIpa(text) } : nil
    }
    private var phonemeKeysClosure: (() -> [(key: String, cls: String)])? {
        isPronounceType(selectedType) ? { engine.getPhonemeKeys() } : nil
    }
    private var previewPhonemeClosure: ((String) -> Void)? {
        isPronounceType(selectedType) ? { key in engine.previewPhoneme(key) } : nil
    }

    @ViewBuilder
    private var entryListView: some View {
        if langFilter.isEmpty || selectedType.isEmpty {
            Spacer()
            Text("Select a type and language to view dictionary entries.")
                .foregroundColor(.secondary)
                .padding()
            Spacer()
        } else if filteredEntries.isEmpty {
            Spacer()
            VStack(spacing: 8) {
                if !activeSearch.isEmpty {
                    Text("No matches for \"\(activeSearch)\"")
                        .foregroundColor(.secondary)
                } else {
                    Text("No \(dictTypeLabel(selectedType).lowercased()) entries yet")
                        .font(.headline)
                        .foregroundColor(.secondary)
                    Text("Want to be the first to add one? Tap the + (add) button above.")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)
                }
            }
            .padding()
            Spacer()
        } else {
            List {
                ForEach(filteredEntries) { entry in
                    DictEntryRow(
                        entry: entry,
                        onEdit: { editingEntry = entry },
                        onMask: {
                            engine.maskDictEntry(fromText: entry.fromText,
                                                 masked: !entry.masked)
                        },
                        onDelete: {
                            engine.deleteDictEntry(fromText: entry.fromText)
                        },
                        onPreview: (isPronounceType(selectedType) || selectedType == "character") ? {
                            engine.previewDictEntry(from: entry.fromText, to: entry.toText, toIpa: entry.toIpa)
                        } : nil
                    )
                }
                if engine.dictionaryEntries.count < engine.dictionaryTotalCount
                    && activeSearch.isEmpty {
                    Button(action: {
                        engine.loadDictionary(
                            langTag: langFilter,
                            subType: backendDictType(selectedType),
                            offset: loadedCount,
                            limit: 100,
                            append: true
                        )
                        loadedCount += 100
                    }) {
                        HStack {
                            Spacer()
                            Text("Load more")
                            Spacer()
                        }
                    }
                    .accessibilityLabel("Load more entries, showing \(engine.dictionaryEntries.count) of \(engine.dictionaryTotalCount)")
                }
            }
            .listStyle(.plain)
        }
    }

    @ViewBuilder
    private var moreOptionsMenu: some View {
        if isPronounceType(selectedType) || selectedType == "character" {
            Button(action: {
                exportUserOnly = false
                showExportPicker = true
            }) {
                Label("Export all", systemImage: "square.and.arrow.up")
            }
        }
        Button(action: {
            exportUserOnly = true
            showExportPicker = true
        }) {
            Label("Export changed", systemImage: "square.and.arrow.up")
        }
        if isPronounceType(selectedType) || selectedType == "character" {
            if let url = exportDictToTempFile(userOnly: false) {
                ShareLink("Share all", item: url)
            }
        }
        if let url = exportDictToTempFile(userOnly: true) {
            ShareLink("Share changed", item: url)
        }
        Button(action: { showImportPicker = true }) {
            Label("Import", systemImage: "square.and.arrow.down")
        }
        .disabled(selectedType != "pronounce" && selectedType != "character")
        Divider()
        Button(action: {
            let mainKeys = Set(engine.dictionaryEntries.filter { $0.source == "main" }.map { $0.fromText.lowercased() })
            let dupes = engine.dictionaryEntries.filter { $0.source == "user" && mainKeys.contains($0.fromText.lowercased()) }
            if dupes.count > 500 {
                statusMessage = "Too many duplicates (\(dupes.count)), remove manually"
            } else if dupes.isEmpty {
                statusMessage = "No duplicates found"
            } else {
                for d in dupes { engine.deleteDictEntry(fromText: d.fromText) }
                statusMessage = "Removed \(dupes.count) duplicates"
            }
        }) {
            Label("Remove duplicates", systemImage: "doc.on.doc")
        }
        Button(action: { showExcludeSheet = true }) {
            Label("Exclude dictionaries", systemImage: "eye.slash")
        }
        if isPronounceType(selectedType) {
            Button(action: { showExcludeCategoriesSheet = true }) {
                Label("Exclude categories", systemImage: "tag.slash")
            }
        }
        Button(role: .destructive, action: { showRemoveConfirm = true }) {
            Label("Remove changed entries", systemImage: "trash")
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            // Type dropdown + Language dropdown row
            HStack(spacing: 8) {
                // Type picker
                Menu {
                    ForEach(displayTypes) { dt in
                        let label = dictTypeLabel(dt.type) + " (\(dt.count))"
                        Button(label) { selectedType = dt.type }
                    }
                } label: {
                    Text(typePickerLabel)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 6)
                        .background(Color.secondary.opacity(0.15))
                        .cornerRadius(8)
                }
                .accessibilityLabel({
                    var desc = "Type: "
                    if selectedType.isEmpty {
                        desc += "Select type"
                    } else {
                        desc += dictTypeLabel(selectedType)
                        desc += ", \(filteredEntries.count) entries"
                    }
                    return desc
                }())

                // Language picker
                Menu {
                    ForEach(engine.editorLanguages, id: \.self) { lang in
                        Button(lang) { langFilter = lang }
                    }
                } label: {
                    Text(langFilter.isEmpty ? "Select language" : langFilter)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 6)
                        .background(Color.secondary.opacity(0.15))
                        .cornerRadius(8)
                }
                .accessibilityLabel("Language: \(langFilter.isEmpty ? "Select language" : langFilter)")

                Spacer()
            }
            .padding(.horizontal)
            .padding(.top, 8)

            // Search field
            TextField("Search", text: $searchQuery)
                .textFieldStyle(.roundedBorder)
                .autocorrectionDisabled()
#if os(iOS)
                .textInputAutocapitalization(.never)
#endif
                .padding(.horizontal)
                .padding(.vertical, 4)

            // Entry count + Add button + More options row
            HStack {
                Text("Showing \(filteredEntries.count) of \(selectedType == "user" ? filteredEntries.count : engine.dictionaryTotalCount) entries")
                    .font(.caption)
                    .foregroundColor(.secondary)

                Spacer()

                Button(action: { showAddSheet = true }) {
                    Image(systemName: "plus")
                        .font(.body)
                }
                .accessibilityLabel("Add entry")
                .disabled(langFilter.isEmpty || selectedType.isEmpty)

                // More options menu
                Menu {
                    moreOptionsMenu
                } label: {
                    Image(systemName: "ellipsis.circle")
                        .font(.body)
                }
                .accessibilityLabel("More options")
#if os(iOS)
                .accessibilityElement()
                .accessibilityAddTraits(.isButton)
                .accessibilityHint("Double tap to show actions")
#endif
            }
            .padding(.horizontal)
            .padding(.vertical, 4)

            // Entry list
            entryListView
        }
        .onAppear {
            if !engineStarted {
                if engine.start() { engineStarted = true }
            }
            if engineStarted {
                engine.loadEditorLanguages()
                engine.loadDictTypes()
            }
            // Default type if not yet set (handles tab switching recreation).
            if selectedType.isEmpty && !displayTypes.isEmpty {
                let pronounce = displayTypes.first(where: { $0.type == "pronounce" })
                selectedType = pronounce?.type ?? (displayTypes.first?.type ?? "")
            }
            // Default language from system locale if not yet set.
            if langFilter.isEmpty && !engine.editorLanguages.isEmpty {
                let engineLang = engine.currentEngineLangTag()
                if engine.editorLanguages.contains(engineLang) {
                    langFilter = engineLang
                } else {
                    // Try system locale (e.g. "es" from "es-ES").
                    let sysLang = Locale.current.language.languageCode?.identifier ?? ""
                    let match = engine.editorLanguages.first(where: { $0.hasPrefix(sysLang) })
                    langFilter = match ?? engine.editorLanguages.first ?? ""
                }
            }
        }
        .onChange(of: engine.editorLanguages) { langs in
            // Default language to engine's current language
            if langFilter.isEmpty && !langs.isEmpty {
                let engineLang = engine.currentEngineLangTag()
                langFilter = langs.contains(engineLang) ? engineLang : (langs.first ?? "")
            }
        }
        .onChange(of: engine.dictTypes) { types in
            // Default type to "pronounce" if available
            if selectedType.isEmpty && !types.isEmpty {
                let pronounce = types.first(where: { $0.type == "pronounce" })
                selectedType = pronounce?.type ?? (types.first?.type ?? "")
            }
        }
        .onChange(of: selectedType) { _ in reloadEntries() }
        .onChange(of: langFilter) { _ in reloadEntries() }
        .onChange(of: activeSearch) { _ in reloadEntries() }
        .task(id: searchQuery) {
            if searchQuery.isEmpty {
                activeSearch = ""
                return
            }
            try? await Task.sleep(nanoseconds: 300_000_000) // 300ms debounce
            if !Task.isCancelled {
                activeSearch = searchQuery
            }
        }
        .sheet(isPresented: $showAddSheet) {
            DictEntrySheet(
                title: "Add Entry",
                dictType: selectedType,
                onSave: { from, to, cat, fIpa, tIpa in
                    engine.addDictEntry(fromText: from, toText: to, category: cat,
                                        fromIpa: fIpa, toIpa: tIpa)
                },
                onPreview: previewClosure,
                onTextToIpa: textToIpaClosure,
                onGetPhonemeKeys: phonemeKeysClosure,
                onPreviewPhoneme: previewPhonemeClosure
            )
        }
        .sheet(item: $editingEntry) { entry in
            DictEntrySheet(
                title: "Edit Entry",
                dictType: selectedType,
                initialFrom: entry.fromText,
                initialTo: entry.toText,
                initialCategory: entry.category,
                initialFromIpa: entry.fromIpa,
                initialToIpa: entry.toIpa,
                isEdit: true,
                onSave: { from, to, cat, fIpa, tIpa in
                    if from != entry.fromText {
                        engine.deleteDictEntry(fromText: entry.fromText)
                    }
                    engine.addDictEntry(fromText: from, toText: to, category: cat,
                                        fromIpa: fIpa, toIpa: tIpa)
                },
                onPreview: previewClosure,
                onTextToIpa: textToIpaClosure,
                onGetPhonemeKeys: phonemeKeysClosure,
                onPreviewPhoneme: previewPhonemeClosure
            )
        }
        .alert("Remove changed entries", isPresented: $showRemoveConfirm) {
            Button("Remove", role: .destructive) {
                let userEntries = engine.dictionaryEntries.filter { $0.source == "user" }
                for e in userEntries {
                    engine.deleteDictEntry(fromText: e.fromText)
                }
                let maskedEntries = engine.dictionaryEntries.filter { $0.source == "main" && $0.masked }
                for e in maskedEntries {
                    engine.maskDictEntry(fromText: e.fromText, masked: false)
                }
                statusMessage = "Removed \(userEntries.count + maskedEntries.count) user changes"
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("This will remove all user overrides for \(dictTypeLabel(selectedType)) in \(langFilter). This cannot be undone.")
        }
        .sheet(isPresented: $showExcludeSheet) {
            ExcludeDictionariesSheet(engine: engine, langTag: langFilter)
        }
        .sheet(isPresented: $showExcludeCategoriesSheet) {
            ExcludeCategoriesSheet(engine: engine, langTag: langFilter)
        }
        .fileExporter(
            isPresented: $showExportPicker,
            document: DictTsvDocument(entries: engine.dictionaryEntries, userOnly: exportUserOnly),
            contentType: .tabSeparatedText,
            defaultFilename: exportUserOnly
                ? "\(selectedType)_\(langFilter)_changed.tsv"
                : "\(selectedType)_\(langFilter).tsv"
        ) { result in
            if case .success = result {
                statusMessage = exportUserOnly ? "Exported changed entries" : "Exported all entries"
            } else if case .failure(let error) = result {
                statusMessage = "Export failed: \(error.localizedDescription)"
            }
        }
        .fileImporter(
            isPresented: $showImportPicker,
            allowedContentTypes: [.tabSeparatedText, .plainText, .data],
            allowsMultipleSelection: false
        ) { result in
            if case .success(let urls) = result, let url = urls.first {
                importDictTsv(from: url)
            }
        }
        .alert("Result", isPresented: Binding(
            get: { statusMessage != nil },
            set: { if !$0 { statusMessage = nil } }
        )) {
            Button("OK") { statusMessage = nil }
        } message: {
            Text(statusMessage ?? "")
        }
    }

    private func reloadEntries() {
        guard !langFilter.isEmpty, !selectedType.isEmpty else { return }
        // Re-apply user overrides so they show up in the query results.
        engine.reapplyDictOverrides(langFilter)
        engine.loadDictTypes(langTag: langFilter)
        engine.loadDictionary(
            langTag: langFilter,
            subType: backendDictType(selectedType),
            offset: 0,
            limit: 100,
            search: activeSearch
        )
        loadedCount = 100
    }

    private func exportDictToTempFile(userOnly: Bool) -> URL? {
        let entries = userOnly
            ? engine.dictionaryEntries.filter { $0.source == "user" }
            : engine.dictionaryEntries.filter { !$0.masked }
        if entries.isEmpty { return nil }
        var tsv = "# from_text\tto_text\tfrom_ipa\tto_ipa\tcategory\n"
        for e in entries {
            tsv += "\(e.fromText)\t\(e.toText)\t\(e.fromIpa)\t\(e.toIpa)\t\(e.category)\n"
        }
        let filename = userOnly
            ? "\(langFilter)-user.tsv"
            : "\(selectedType)_\(langFilter).tsv"
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent(filename)
        guard let _ = try? tsv.write(to: url, atomically: true, encoding: .utf8) else {
            return nil
        }
        return url
    }

    private func importDictTsv(from url: URL) {
        let accessing = url.startAccessingSecurityScopedResource()
        defer { if accessing { url.stopAccessingSecurityScopedResource() } }

        let picked = tgsbReadImportText(url)
        guard let content = picked.text else {
            statusMessage = picked.tooLarge ? "File is too large" : "Could not read file"
            return
        }
        if content.isEmpty {
            statusMessage = "File is empty"
            return
        }
        var count = 0
        for line in content.components(separatedBy: .newlines) {
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            if trimmed.isEmpty || trimmed.hasPrefix("#") { continue }
            let parts = trimmed.components(separatedBy: "\t")
            if parts.count >= 2 {
                let word = parts[0].trimmingCharacters(in: .whitespaces)
                let value = parts[1].trimmingCharacters(in: .whitespaces)
                if !word.isEmpty && !value.isEmpty {
                    engine.addDictEntry(fromText: word, toText: value)
                    count += 1
                }
            }
        }
        statusMessage = "Imported \(count) entries"
    }
}

// MARK: - Entry Row

private struct DictEntryRow: View {
    let entry: TgsbEngine.DictEntry
    let onEdit: () -> Void
    let onMask: () -> Void
    let onDelete: () -> Void
    var onPreview: (() -> Void)? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("\(entry.fromText) \u{2192} \(entry.toText)")
                .foregroundColor(entry.masked ? .secondary : .primary)

            HStack(spacing: 4) {
                Text(entry.source)
                    .font(.caption2)
                    .foregroundColor(.secondary)
                if !entry.category.isEmpty {
                    Text("\u{00B7} \(entry.category)")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }
                if entry.masked {
                    Text("\u{00B7} excluded")
                        .font(.caption2)
                        .foregroundColor(.red)
                }
            }
        }
        .contentShape(Rectangle())
        .onTapGesture { onEdit() }
        .swipeActions(edge: .trailing) {
            if entry.source == "user" {
                Button("Delete", role: .destructive) { onDelete() }
            }
            if entry.source == "main" {
                Button(entry.masked ? "Include" : "Exclude") { onMask() }
                    .tint(entry.masked ? .green : .orange)
            }
        }
        .swipeActions(edge: .leading) {
            if let onPreview = onPreview {
                Button("Preview") { onPreview() }
                    .tint(.blue)
            }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel({
            let label: String
            if !entry.toIpa.isEmpty {
                label = "\(entry.fromText), pronunciation override"
            } else {
                label = "\(entry.fromText) maps to \(entry.toText)"
            }
            return label + ", \(entry.source) dictionary" +
                (entry.masked ? ", excluded" : "") +
                (entry.category.isEmpty ? "" : ", \(entry.category)")
        }())
        .accessibilityAction(.default) { onEdit() }
        .accessibilityAction(named: "Edit") { onEdit() }
        .accessibilityAddTraits(.isButton)
        .accessibilityHint("Double tap to edit")
    }
}

// MARK: - Add / Edit Sheet

private struct DictEntrySheet: View {
    let title: String
    let dictType: String
    @State var fromText: String = ""
    @State var toText: String = ""
    @State var category: String = ""
    @State var fromIpa: String = ""
    @State var toIpa: String = ""
    @State var caseSensitive: Bool = false
    var isEdit: Bool = false
    let onSave: (String, String, String, String, String) -> Void
    var onPreview: ((String, String, String) -> Void)? = nil
    var onTextToIpa: ((String) -> String)? = nil
    var onGetPhonemeKeys: (() -> [(key: String, cls: String)])? = nil
    var onPreviewPhoneme: ((String) -> Void)? = nil
    @Environment(\.dismiss) private var dismiss
    @State private var showPhonemePicker = false
    @State private var phonemePickerTarget = ""

    init(title: String, dictType: String, initialFrom: String = "",
         initialTo: String = "", initialCategory: String = "",
         initialFromIpa: String = "", initialToIpa: String = "",
         isEdit: Bool = false,
         onSave: @escaping (String, String, String, String, String) -> Void,
         onPreview: ((String, String, String) -> Void)? = nil,
         onTextToIpa: ((String) -> String)? = nil,
         onGetPhonemeKeys: (() -> [(key: String, cls: String)])? = nil,
         onPreviewPhoneme: ((String) -> Void)? = nil) {
        self.title = title
        self.dictType = dictType
        self._fromText = State(initialValue: initialFrom)
        self._toText = State(initialValue: initialTo)
        self._category = State(initialValue: initialCategory)
        self._fromIpa = State(initialValue: initialFromIpa)
        self._toIpa = State(initialValue: initialToIpa)
        self._caseSensitive = State(initialValue: initialFrom != initialFrom.lowercased())
        self.isEdit = isEdit
        self.onSave = onSave
        self.onPreview = onPreview
        self.onTextToIpa = onTextToIpa
        self.onGetPhonemeKeys = onGetPhonemeKeys
        self.onPreviewPhoneme = onPreviewPhoneme
    }

    private var toLabel: String {
        switch dictType {
        case "stress":    return "Stress pattern"
        case "compound":  return "Split as"
        case "character": return "Description"
        default:          return "Pronounce as"
        }
    }

    private var toHelperText: String? {
        switch dictType {
        case "stress":    return "Space-separated digits: 1 = primary, 2 = secondary, 0 = none"
        case "compound":  return "Space-separated parts, e.g. \"lock box\""
        case "character": return "How this character is spoken, e.g. \"a acentuada\""
        default:          return nil
        }
    }

    private var fromLabel: String {
        dictType == "character" ? "Symbol" : "Word"
    }

    private var showCategory: Bool {
        return dictType != "stress" && dictType != "compound" && dictType != "character"
    }

    var body: some View {
        NavigationView {
            Form {
                Section(fromLabel) {
                    TextField(fromLabel, text: $fromText)
                        .autocorrectionDisabled()
                        #if os(iOS)
                        .textInputAutocapitalization(.never)
                        #endif
                        .accessibilityLabel(fromLabel)
                        .onChange(of: fromText) { newValue in
                            if dictType == "character" {
                                let stripped = newValue.replacingOccurrences(of: " ", with: "")
                                if stripped != newValue { fromText = stripped }
                            }
                        }
                }
                Section(toLabel) {
                    TextField(toLabel, text: $toText)
                        .autocorrectionDisabled()
                        #if os(iOS)
                        .textInputAutocapitalization(.never)
                        #endif
                    if let helper = toHelperText {
                        Text(helper)
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }
                if dictType == "pronounce" {
                    Section("IPA (optional)") {
                        TextField("From IPA", text: $fromIpa)
                            .autocorrectionDisabled()
                            #if os(iOS)
                            .textInputAutocapitalization(.never)
                            #endif
                        if onGetPhonemeKeys != nil {
                            Button("Insert phoneme into From IPA") {
                                phonemePickerTarget = "fromIpa"
                                showPhonemePicker = true
                            }
                        }
                        TextField("To IPA (overrides respelling)", text: $toIpa)
                            .autocorrectionDisabled()
                            #if os(iOS)
                            .textInputAutocapitalization(.never)
                            #endif
                        if onGetPhonemeKeys != nil {
                            Button("Insert phoneme into To IPA") {
                                phonemePickerTarget = "toIpa"
                                showPhonemePicker = true
                            }
                        }
                        if let onTextToIpa = onTextToIpa {
                            Button("Fill IPA from eSpeak") {
                                let from = fromText.trimmingCharacters(in: .whitespaces)
                                let to = toText.trimmingCharacters(in: .whitespaces)
                                if from.isEmpty && to.isEmpty { return }
                                if !from.isEmpty && fromIpa.trimmingCharacters(in: .whitespaces).isEmpty {
                                    fromIpa = onTextToIpa(from)
                                }
                                if !to.isEmpty && toIpa.trimmingCharacters(in: .whitespaces).isEmpty {
                                    toIpa = onTextToIpa(to)
                                }
                            }
                            .disabled(
                                fromText.trimmingCharacters(in: .whitespaces).isEmpty &&
                                toText.trimmingCharacters(in: .whitespaces).isEmpty
                            )
                        }
                    }
                    Section {
                        Toggle("Match capitalization", isOn: $caseSensitive)
                    }
                }
                if showCategory {
                    Section("Category (optional)") {
                        TextField("Category", text: $category)
                            .autocorrectionDisabled()
                            #if os(iOS)
                            .textInputAutocapitalization(.never)
                            #endif
                    }
                }
            }
            .navigationTitle(title)
#if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
#endif
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                if let onPreview = onPreview {
                    ToolbarItem(placement: .automatic) {
                        Button("Preview") {
                            let word = fromText.trimmingCharacters(in: .whitespaces)
                            let replacement = toText.trimmingCharacters(in: .whitespaces)
                            if !word.isEmpty && !replacement.isEmpty {
                                onPreview(word, replacement, toIpa.trimmingCharacters(in: .whitespaces))
                            }
                        }
                        .disabled(
                            fromText.trimmingCharacters(in: .whitespaces).isEmpty ||
                            toText.trimmingCharacters(in: .whitespaces).isEmpty
                        )
                    }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        var word = fromText.trimmingCharacters(in: .whitespaces)
                        if !caseSensitive && dictType == "pronounce" {
                            word = word.lowercased()
                        }
                        onSave(
                            word,
                            toText.trimmingCharacters(in: .whitespaces),
                            category.trimmingCharacters(in: .whitespaces),
                            fromIpa.trimmingCharacters(in: .whitespaces),
                            toIpa.trimmingCharacters(in: .whitespaces)
                        )
                        dismiss()
                    }
                    .disabled(
                        fromText.trimmingCharacters(in: .whitespaces).isEmpty ||
                        toText.trimmingCharacters(in: .whitespaces).isEmpty
                    )
                }
            }
        }
#if os(iOS)
        .presentationDetents([.medium])
#endif
        .sheet(isPresented: $showPhonemePicker) {
            if let getKeys = onGetPhonemeKeys {
                PhonemePickerSheet(
                    keys: getKeys(),
                    onSelect: { key in
                        if phonemePickerTarget == "fromIpa" {
                            fromIpa = fromIpa.isEmpty ? key : "\(fromIpa) \(key)"
                        } else {
                            toIpa = toIpa.isEmpty ? key : "\(toIpa) \(key)"
                        }
                        showPhonemePicker = false
                    },
                    onPreview: onPreviewPhoneme
                )
            }
        }
    }
}

// MARK: - Phoneme Picker Sheet

private struct PhonemePickerSheet: View {
    let keys: [(key: String, cls: String)]
    let onSelect: (String) -> Void
    var onPreview: ((String) -> Void)? = nil
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationView {
            List {
                if keys.isEmpty {
                    Text("No phonemes available for the current language.")
                        .foregroundColor(.secondary)
                } else {
                    ForEach(keys, id: \.key) { entry in
                        Button(action: { onSelect(entry.key) }) {
                            Text("\(entry.key) (\(entry.cls))")
                        }
                        .accessibilityAction(named: "Preview") {
                            onPreview?(entry.key)
                        }
                    }
                }
            }
            .navigationTitle("Insert phoneme")
#if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
#endif
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
            }
        }
#if os(iOS)
        .presentationDetents([.medium])
#endif
    }
}

// MARK: - Exclude Dictionaries Sheet

private struct ExcludeDictionariesSheet: View {
    @ObservedObject var engine: TgsbEngine
    let langTag: String
    @Environment(\.dismiss) private var dismiss
    @State private var config: [(type: String, enabled: Bool)] = []

    var body: some View {
        NavigationView {
            Form {
                Section {
                    Text("Uncheck a dictionary type to exclude it for \(langTag).")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                Section {
                    ForEach(config.indices, id: \.self) { idx in
                        Toggle(dictTypeLabel(config[idx].type), isOn: Binding(
                            get: { config[idx].enabled },
                            set: { newVal in
                                config[idx].enabled = newVal
                                engine.setDictTypeEnabled(
                                    langTag: langTag,
                                    type: config[idx].type,
                                    enabled: newVal
                                )
                            }
                        ))
                    }
                }
            }
            .navigationTitle("Exclude dictionaries")
#if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
#endif
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .onAppear {
            config = engine.loadDictConfig(langTag: langTag)
                .sorted(by: { $0.type < $1.type })
        }
#if os(iOS)
        .presentationDetents([.medium])
#endif
    }
}

// MARK: - Exclude Categories Sheet

private struct ExcludeCategoriesSheet: View {
    @ObservedObject var engine: TgsbEngine
    let langTag: String
    @Environment(\.dismiss) private var dismiss

    private var categories: [String] {
        let cats = Set(engine.dictionaryEntries
            .filter { !$0.category.isEmpty }
            .map { $0.category })
        return cats.sorted()
    }

    private func isCategoryIncluded(_ cat: String) -> Bool {
        engine.dictionaryEntries.contains {
            $0.category.caseInsensitiveCompare(cat) == .orderedSame && !$0.masked
        }
    }

    var body: some View {
        NavigationView {
            Form {
                if categories.isEmpty {
                    Section {
                        Text("No categories found in the current dictionary.")
                            .foregroundColor(.secondary)
                    }
                } else {
                    Section {
                        Text("Uncheck a category to exclude all its entries.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    Section {
                        ForEach(categories, id: \.self) { cat in
                            let count = engine.dictionaryEntries.count {
                                $0.category.caseInsensitiveCompare(cat) == .orderedSame
                            }
                            Toggle("\(cat) (\(count))", isOn: Binding(
                                get: { isCategoryIncluded(cat) },
                                set: { newVal in
                                    engine.maskDictCategory(category: cat, masked: !newVal)
                                }
                            ))
                        }
                    }
                }
            }
            .navigationTitle("Exclude categories")
#if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
#endif
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
#if os(iOS)
        .presentationDetents([.medium])
#endif
    }
}

// MARK: - TSV File Document for Export

struct DictTsvDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.tabSeparatedText, .plainText] }

    let content: String

    init(entries: [TgsbEngine.DictEntry], userOnly: Bool) {
        let filtered = userOnly
            ? entries.filter { $0.source == "user" }
            : entries.filter { !$0.masked }
        var tsv = ""
        for e in filtered {
            tsv += "\(e.fromText)\t\(e.toText)\n"
        }
        content = tsv
    }

    init(configuration: ReadConfiguration) throws {
        if let data = configuration.file.regularFileContents {
            content = String(data: data, encoding: .utf8) ?? ""
        } else {
            content = ""
        }
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        let data = content.data(using: .utf8) ?? Data()
        return FileWrapper(regularFileWithContents: data)
    }
}
