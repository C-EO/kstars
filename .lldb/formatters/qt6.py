#
# LLDB data formatters for Qt6 types
#
# Based on original Qt5 work by:
#   SPDX-FileCopyrightText: 2016 Aetf <aetf@unlimitedcodeworks.xyz>
#
# Qt6 port:
#   Updated internal layouts for Qt6 (QArrayDataPointer, QHashPrivate, std::map-based QMap)
#
# SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#

from __future__ import print_function

import time
import datetime as dt
import locale

import lldb

from helpers import (HiddenMemberProvider, quote, unichr, toSBPointer, Iterator,
                     validAddr, validPointer, invoke, rename, canonicalized_type_name)


def __lldb_init_module(debugger, unused):
    debugger.HandleCommand('type synthetic add QString -w kdevelop-qt6 -l qt6.QStringFormatter')
    debugger.HandleCommand('type summary add QString -w kdevelop-qt6 -F qt6.QStringSummaryProvider')

    debugger.HandleCommand('type summary add QChar -w kdevelop-qt6 -F qt6.QCharSummaryProvider')

    debugger.HandleCommand('type synthetic add QByteArray -w kdevelop-qt6 -l qt6.QByteArrayFormatter')
    debugger.HandleCommand('type summary add QByteArray -w kdevelop-qt6 -e -F qt6.QByteArraySummaryProvider')

    debugger.HandleCommand('type synthetic add -x "^QList<.+>$" -w kdevelop-qt6 -l qt6.QListFormatter')
    debugger.HandleCommand('type summary add -x "^QList<.+>$" -w kdevelop-qt6 -e -s "<size=${svar%#}>"')

    debugger.HandleCommand('type synthetic add QStringList -w kdevelop-qt6 -l qt6.QStringListFormatter')
    debugger.HandleCommand('type summary add QStringList -w kdevelop-qt6 -e -s "<size=${svar%#}>"')

    debugger.HandleCommand('type synthetic add -x "^QQueue<.+>$" -w kdevelop-qt6 -l qt6.QQueueFormatter')
    debugger.HandleCommand('type summary add -x "^QQueue<.+>$" -w kdevelop-qt6 -e -s "<size=${svar%#}>"')

    # In Qt6, QVector<T> is an alias for QList<T>
    debugger.HandleCommand('type synthetic add -x "^QVector<.+>$" -w kdevelop-qt6 -l qt6.QListFormatter')
    debugger.HandleCommand('type summary add -x "^QVector<.+>$" -w kdevelop-qt6 -e -s "<size=${svar%#}>"')

    debugger.HandleCommand('type synthetic add -x "^QStack<.+>$" -w kdevelop-qt6 -l qt6.QStackFormatter')
    debugger.HandleCommand('type summary add -x "^QStack<.+>$" -w kdevelop-qt6 -e -s "<size=${svar%#}>"')

    # QLinkedList was removed in Qt6 — no formatter needed

    debugger.HandleCommand('type synthetic add -x "^QMap<.+>$" -w kdevelop-qt6 -l qt6.QMapFormatter')
    debugger.HandleCommand('type summary add -x "^QMap<.+>$" -w kdevelop-qt6 -e -s "<size=${svar%#}>"')

    debugger.HandleCommand('type synthetic add -x "^QMultiMap<.+>$" -w kdevelop-qt6 -l qt6.QMultiMapFormatter')
    debugger.HandleCommand('type summary add -x "^QMultiMap<.+>$" -w kdevelop-qt6 -e -s "<size=${svar%#}>"')

    debugger.HandleCommand('type synthetic add -x "^QHash<.+>$" -w kdevelop-qt6 -l qt6.QHashFormatter')
    debugger.HandleCommand('type summary add -x "^QHash<.+>$" -w kdevelop-qt6 -e -s "<size=${svar%#}>"')

    debugger.HandleCommand('type synthetic add -x "^QMultiHash<.+>$" -w kdevelop-qt6 -l qt6.QMultiHashFormatter')
    debugger.HandleCommand('type summary add -x "^QMultiHash<.+>$" -w kdevelop-qt6 -e -s "<size=${svar%#}>"')

    debugger.HandleCommand('type synthetic add -x "^QSet<.+>$" -w kdevelop-qt6 -l qt6.QSetFormatter')
    debugger.HandleCommand('type summary add -x "^QSet<.+>$" -w kdevelop-qt6 -e -s "<size=${svar%#}>"')

    debugger.HandleCommand('type synthetic add QDate -w kdevelop-qt6 -l qt6.QDateFormatter')
    debugger.HandleCommand('type summary add QDate -w kdevelop-qt6 -e -F qt6.QDateSummaryProvider')

    debugger.HandleCommand('type synthetic add QTime -w kdevelop-qt6 -l qt6.QTimeFormatter')
    debugger.HandleCommand('type summary add QTime -w kdevelop-qt6 -e -F qt6.QTimeSummaryProvider')

    debugger.HandleCommand('type synthetic add QDateTime -w kdevelop-qt6 -l qt6.QDateTimeFormatter')
    debugger.HandleCommand('type summary add QDateTime -w kdevelop-qt6 -e -F qt6.QDateTimeSummaryProvider')

    debugger.HandleCommand('type synthetic add QUrl -w kdevelop-qt6 -l qt6.QUrlFormatter')
    debugger.HandleCommand('type summary add QUrl -w kdevelop-qt6 -e -F qt6.QUrlSummaryProvider')

    debugger.HandleCommand('type synthetic add QUuid -w kdevelop-qt6 -l qt6.QUuidFormatter')
    debugger.HandleCommand('type summary add QUuid -w kdevelop-qt6 -F qt6.QUuidSummaryProvider')

    debugger.HandleCommand('type category enable kdevelop-qt6')


# ---------------------------------------------------------------------------
# QString
# Qt6 internal layout: QString holds a QStringPrivate (= QArrayDataPointer<char16_t>)
#   struct QArrayDataPointer { QTypedArrayData<T> *d; T *ptr; qsizetype size; }
# So: valobj['d']['ptr'] is char16_t*, valobj['d']['size'] is element count.
# ---------------------------------------------------------------------------

def printableQString(valobj):
    """Return (unicode_str, data_ptr_int, byte_length) or (None, 0, 0) on failure."""
    if not valobj.IsValid():
        return None, 0, 0
    try:
        d = valobj.GetChildMemberWithName('d')
        if not d.IsValid():
            return None, 0, 0

        # Qt6: QArrayDataPointer has a 'ptr' member directly
        ptr_val = d.GetChildMemberWithName('ptr')
        size_val = d.GetChildMemberWithName('size')

        if not ptr_val.IsValid() or not size_val.IsValid():
            return None, 0, 0

        size = size_val.GetValueAsSigned(-1)
        if size < 0:
            return None, 0, 0
        if size == 0:
            return u'', ptr_val.GetValueAsUnsigned(0), 0

        cap_size = min(size, HiddenMemberProvider._capping_size())
        too_large = u'...' if size > HiddenMemberProvider._capping_size() else u''

        pointer = ptr_val.GetValueAsUnsigned(0)
        byte_length = cap_size * 2  # UTF-16: 2 bytes per char

        error = lldb.SBError()
        raw = valobj.GetProcess().ReadMemory(pointer, byte_length, error)
        if error.Success():
            content = raw.decode('utf-16-le', errors='replace')
            return content + too_large, pointer, byte_length
    except Exception:
        pass
    return None, 0, 0


def QStringSummaryProvider(valobj, internal_dict):
    if valobj.IsValid():
        printable, _, _ = printableQString(valobj)
        if printable is not None:
            return quote(printable)
    return '<Invalid>'


class QStringFormatter(HiddenMemberProvider):
    """LLDB synthetic provider for QString (Qt6)"""

    def __init__(self, valobj, internal_dict):
        super(QStringFormatter, self).__init__(valobj, internal_dict)
        self._qchar_type = valobj.GetTarget().FindFirstType('QChar')
        self._qchar_size = self._qchar_type.GetByteSize() if self._qchar_type.IsValid() else 2

    def _update(self):
        printable, data_pointer, byte_length = printableQString(self.valobj)
        str_length = byte_length // 2
        if printable is not None:
            for idx in range(str_length):
                var = self.valobj.CreateValueFromAddress(
                    '[{}]'.format(idx),
                    data_pointer + idx * self._qchar_size,
                    self._qchar_type)
                self._addChild(var)
            self._num_children = str_length


def QCharSummaryProvider(valobj, internal_dict):
    if valobj.IsValid():
        ucs = valobj.GetChildMemberWithName('ucs').GetValueAsUnsigned(0)
        if ucs == 39:
            return u"'\\''"
        return unichr(ucs).__repr__()[1:]
    return None


# ---------------------------------------------------------------------------
# QByteArray
# Qt6: same QArrayDataPointer layout — d.ptr is char*, d.size is byte count
# ---------------------------------------------------------------------------

def printableQByteArray(valobj):
    """Return (printable_str, data_ptr_int, length) or (None, 0, 0) on failure."""
    if not valobj.IsValid():
        return None, 0, 0
    try:
        d = valobj.GetChildMemberWithName('d')
        if not d.IsValid():
            return None, 0, 0

        ptr_val = d.GetChildMemberWithName('ptr')
        size_val = d.GetChildMemberWithName('size')

        if not ptr_val.IsValid() or not size_val.IsValid():
            return None, 0, 0

        size = size_val.GetValueAsSigned(-1)
        if size < 0:
            return None, 0, 0
        if size == 0:
            return u'', ptr_val.GetValueAsUnsigned(0), 0

        cap_size = min(size, HiddenMemberProvider._capping_size())
        too_large = u'...' if size > HiddenMemberProvider._capping_size() else u''

        pointer = ptr_val.GetValueAsUnsigned(0)

        error = lldb.SBError()
        raw = valobj.GetProcess().ReadMemory(pointer, cap_size, error)
        if error.Success():
            result = []
            for b in raw:
                if isinstance(b, int):
                    c = b
                else:
                    c = ord(b)
                if 32 <= c < 127 and c != ord("'"):
                    result.append(chr(c))
                else:
                    result.append(r'\x{:02x}'.format(c))
            return u''.join(result) + too_large, pointer, cap_size
    except Exception:
        pass
    return None, 0, 0


def QByteArraySummaryProvider(valobj, internal_dict):
    if valobj.IsValid():
        content = valobj.GetChildMemberWithName('(content)')
        if content.IsValid():
            summary = content.GetSummary()
            if summary is not None:
                return summary
        printable, _, _ = printableQByteArray(valobj)
        if printable is not None:
            return 'b"{}"'.format(printable.replace('"', '\\"'))
    return '<Invalid>'


class QByteArrayFormatter(HiddenMemberProvider):
    """LLDB synthetic provider for QByteArray (Qt6)"""

    def __init__(self, valobj, internal_dict):
        super(QByteArrayFormatter, self).__init__(valobj, internal_dict)
        self._char_type = valobj.GetType().GetBasicType(lldb.eBasicTypeChar)
        self._char_size = self._char_type.GetByteSize()

    def _update(self):
        printable, data_pointer, length = printableQByteArray(self.valobj)
        self._num_children = length
        if printable is not None:
            for idx in range(length):
                var = self.valobj.CreateValueFromAddress(
                    '[{}]'.format(idx),
                    data_pointer + idx * self._char_size,
                    self._char_type)
                self._addChild(var)
            quoted = '"{}"'.format(printable.replace('"', '\\"'))
            self._addChild(('(content)', quoted), hidden=True)


# ---------------------------------------------------------------------------
# QList<T>  (Qt6)
# Qt6: QList<T> IS QVector<T>. Internal layout:
#   QList { QListData d; }
#   QListData = QArrayDataPointer<T> { QTypedArrayData<T> *d; T *ptr; qsizetype size; }
# Elements are contiguous at d.ptr[0..size-1].  No more begin/end/array indirection.
# ---------------------------------------------------------------------------

class QListFormatter(HiddenMemberProvider):
    """LLDB synthetic provider for QList<T> / QVector<T> (Qt6)"""

    def __init__(self, valobj, internal_dict):
        super(QListFormatter, self).__init__(valobj, internal_dict)
        self._item_type = valobj.GetType().GetTemplateArgumentType(0)
        self._item_size = self._item_type.GetByteSize() if self._item_type.IsValid() else 0

    def _update(self):
        if not self._item_type.IsValid() or self._item_size == 0:
            return

        d = self.valobj.GetChildMemberWithName('d')
        if not d.IsValid():
            return

        ptr_val = d.GetChildMemberWithName('ptr')
        size_val = d.GetChildMemberWithName('size')

        if not ptr_val.IsValid() or not size_val.IsValid():
            return

        size = size_val.GetValueAsSigned(-1)
        if size <= 0:
            return

        self._num_children = min(size, self._capping_size())
        base_addr = ptr_val.GetValueAsUnsigned(0)

        for idx in range(self._num_children):
            var = self.valobj.CreateValueFromAddress(
                '[{}]'.format(idx),
                base_addr + idx * self._item_size,
                self._item_type)
            self._addChild(var)


class QStringListFormatter(QListFormatter):
    """LLDB synthetic provider for QStringList (Qt6)"""

    def __init__(self, valobj, internal_dict):
        # QStringList is QList<QString> in Qt6
        super(QStringListFormatter, self).__init__(valobj, internal_dict)
        # Override item type to QString explicitly
        qstring_type = valobj.GetTarget().FindFirstType('QString')
        if qstring_type.IsValid():
            self._item_type = qstring_type
            self._item_size = qstring_type.GetByteSize()


class QQueueFormatter(QListFormatter):
    """LLDB synthetic provider for QQueue<T> (Qt6) — inherits QList"""

    def __init__(self, valobj, internal_dict):
        super(QQueueFormatter, self).__init__(valobj.GetChildAtIndex(0), internal_dict)
        self.actualobj = valobj

    def update(self):
        self.valobj = self.actualobj.GetChildAtIndex(0)
        super(QQueueFormatter, self).update()


class QStackFormatter(QListFormatter):
    """LLDB synthetic provider for QStack<T> (Qt6) — inherits QList"""

    def __init__(self, valobj, internal_dict):
        super(QStackFormatter, self).__init__(valobj.GetChildAtIndex(0), internal_dict)
        self.actualobj = valobj

    def update(self):
        self.valobj = self.actualobj.GetChildAtIndex(0)
        super(QStackFormatter, self).update()


# ---------------------------------------------------------------------------
# QMap<K,V>  (Qt6)
# Qt6 QMap is backed by std::map.  The internal chain is:
#   QMap { QMapData<K,V> *d; }   (shared, ref-counted)
#   QMapData<K,V> { std::map<K,V> m; }
# std::map on libstdc++ (Linux):
#   _M_t (_Rb_tree) -> _M_impl -> _M_header (_Rb_tree_node_base)
#   node count: _M_impl._M_node_count
#   iteration: _M_header._M_left is the leftmost node
#   Each _Rb_tree_node<pair<const K,V>>:
#     _M_color, _M_parent, _M_left, _M_right, _M_value_field (pair<const K,V>)
# ---------------------------------------------------------------------------

def _stdmap_size(map_valobj):
    """Extract size from a std::map SBValue (libstdc++ layout)."""
    try:
        impl = map_valobj.GetChildMemberWithName('_M_t') \
                         .GetChildMemberWithName('_M_impl')
        return impl.GetChildMemberWithName('_M_node_count').GetValueAsSigned(0)
    except Exception:
        return 0


def _stdmap_iter(map_valobj):
    """
    Yield SBValues of std::pair<const K, V> for each node in a std::map
    using in-order traversal (libstdc++ _Rb_tree).
    """
    try:
        impl = map_valobj.GetChildMemberWithName('_M_t') \
                         .GetChildMemberWithName('_M_impl')
        header = impl.GetChildMemberWithName('_M_header')
        node_count = impl.GetChildMemberWithName('_M_node_count').GetValueAsSigned(0)
        if node_count <= 0:
            return

        # leftmost node is _M_header._M_left
        current = header.GetChildMemberWithName('_M_left')
        header_addr = header.GetValueAsUnsigned(0)

        visited = 0
        # iterative in-order: keep a stack
        stack = []
        # start at leftmost
        while current.GetValueAsUnsigned(0) != 0 and \
              current.GetValueAsUnsigned(0) != header_addr:
            stack.append(current)
            current = current.GetChildMemberWithName('_M_left')

        while stack and visited < node_count:
            current = stack.pop()
            # _M_value_field is the pair<const K, V>
            pair = current.GetChildMemberWithName('_M_value_field')
            if pair.IsValid():
                yield pair
                visited += 1
            # go right
            right = current.GetChildMemberWithName('_M_right')
            current = right
            while current.GetValueAsUnsigned(0) != 0 and \
                  current.GetValueAsUnsigned(0) != header_addr:
                stack.append(current)
                current = current.GetChildMemberWithName('_M_left')
    except Exception:
        return


class QMapFormatter(HiddenMemberProvider):
    """LLDB synthetic provider for QMap<K,V> (Qt6)"""

    def __init__(self, valobj, internal_dict):
        super(QMapFormatter, self).__init__(valobj, internal_dict)

    def _get_stdmap(self):
        """Navigate to the std::map inside QMap's d pointer."""
        d = self.valobj.GetChildMemberWithName('d')
        if not d.IsValid():
            return None
        # d is a QMapData<K,V>* — dereference to get the struct
        d_deref = d.Dereference()
        if not d_deref.IsValid():
            return None
        return d_deref.GetChildMemberWithName('m')

    def _update(self):
        stdmap = self._get_stdmap()
        if stdmap is None or not stdmap.IsValid():
            return

        self._num_children = 0
        for idx, pair in enumerate(_stdmap_iter(stdmap)):
            if idx >= self._capping_size():
                break
            name = '[{}]'.format(idx)
            var = self.valobj.CreateValueFromData(name, pair.GetData(), pair.GetType())
            self._addChild(var)
            self._num_children += 1


class QMultiMapFormatter(QMapFormatter):
    """LLDB synthetic provider for QMultiMap<K,V> (Qt6) — same layout as QMap"""

    def __init__(self, valobj, internal_dict):
        super(QMultiMapFormatter, self).__init__(valobj, internal_dict)


# ---------------------------------------------------------------------------
# QHash<K,V>  (Qt6)
# Qt6 QHash uses a completely new open-addressing hash table:
#   QHash { QHashPrivate::Data<Node<K,V>> *d; }
#   Data { Span *spans; size_t numBuckets; size_t size; ... }
#   Span { Entry entries[NEntries]; uint8_t offsets[NEntries]; ... }
#     NEntries = 128
#   Entry { storage (aligned char[sizeof(Node)]); }
#   Node<K,V> { K key; V value; }  (for non-multi)
# An offset value of 0xff means the slot is empty.
# ---------------------------------------------------------------------------

QHASH_SPAN_ENTRIES = 128
QHASH_EMPTY_OFFSET = 0xff


class QHashFormatter(HiddenMemberProvider):
    """LLDB synthetic provider for QHash<K,V> (Qt6)"""

    def __init__(self, valobj, internal_dict):
        super(QHashFormatter, self).__init__(valobj, internal_dict)
        self_type = valobj.GetType()
        self._key_type = self_type.GetTemplateArgumentType(0)
        self._val_type = self_type.GetTemplateArgumentType(1)

    def _update(self):
        d = self.valobj.GetChildMemberWithName('d')
        if not d.IsValid() or d.GetValueAsUnsigned(0) == 0:
            return

        d_deref = d.Dereference()
        if not d_deref.IsValid():
            return

        size = d_deref.GetChildMemberWithName('size').GetValueAsSigned(0)
        if size <= 0:
            return

        num_buckets = d_deref.GetChildMemberWithName('numBuckets').GetValueAsUnsigned(0)
        spans = d_deref.GetChildMemberWithName('spans')
        if not spans.IsValid() or num_buckets == 0:
            return

        key_size = self._key_type.GetByteSize()
        val_size = self._val_type.GetByteSize()
        # Node<K,V> layout: key at offset 0, value after key (with alignment)
        # Use natural alignment: align val to its own size (max 8)
        val_align = min(val_size, 8)
        val_offset_in_node = (key_size + val_align - 1) & ~(val_align - 1)
        node_size = val_offset_in_node + val_size

        num_spans = (num_buckets + QHASH_SPAN_ENTRIES - 1) // QHASH_SPAN_ENTRIES
        process = self.valobj.GetProcess()

        found = 0
        cap = self._capping_size()

        for span_idx in range(num_spans):
            if found >= cap:
                break

            span = spans.GetChildAtIndex(span_idx, lldb.eDynamicCanRunTarget, True)
            if not span.IsValid():
                continue

            offsets_member = span.GetChildMemberWithName('offsets')
            entries_member = span.GetChildMemberWithName('entries')
            if not offsets_member.IsValid() or not entries_member.IsValid():
                continue

            offsets_addr = offsets_member.GetValueAsUnsigned(0) if offsets_member.TypeIsPointerType() \
                           else offsets_member.GetLoadAddress()
            entries_addr = entries_member.GetValueAsUnsigned(0) if entries_member.TypeIsPointerType() \
                           else entries_member.GetLoadAddress()

            # Read all 128 offsets at once
            error = lldb.SBError()
            offsets_raw = process.ReadMemory(offsets_addr, QHASH_SPAN_ENTRIES, error)
            if not error.Success():
                continue

            for slot in range(QHASH_SPAN_ENTRIES):
                if found >= cap:
                    break
                offset = offsets_raw[slot] if isinstance(offsets_raw[slot], int) \
                         else ord(offsets_raw[slot])
                if offset == QHASH_EMPTY_OFFSET:
                    continue

                # Each Entry is a storage buffer of node_size bytes
                entry_addr = entries_addr + offset * node_size

                # Read key
                key_raw = process.ReadMemory(entry_addr, key_size, error)
                if not error.Success():
                    continue
                key_data = lldb.SBData()
                key_data.SetDataWithOwnership(
                    lldb.eByteOrderLittle if process.GetByteOrder() == lldb.eByteOrderLittle
                    else lldb.eByteOrderBig,
                    process.GetAddressByteSize(),
                    bytearray(key_raw))
                key_var = self.valobj.CreateValueFromData(
                    '(key)', key_data, self._key_type)

                # Read value
                val_raw = process.ReadMemory(entry_addr + val_offset_in_node, val_size, error)
                if not error.Success():
                    continue
                val_data = lldb.SBData()
                val_data.SetDataWithOwnership(
                    lldb.eByteOrderLittle if process.GetByteOrder() == lldb.eByteOrderLittle
                    else lldb.eByteOrderBig,
                    process.GetAddressByteSize(),
                    bytearray(val_raw))
                val_var = self.valobj.CreateValueFromData(
                    '(value)', val_data, self._val_type)

                # Represent as a synthetic child named by index; show key summary
                key_summary = key_var.GetSummary() or key_var.GetValue() or '?'
                name = '[{}]'.format(found)
                # We add value as the child — key is visible as the name label
                # Use a combined display via the value child
                self._addChild(val_var if not key_var.IsValid() else key_var)
                # Rename last added child to show key
                if self._members:
                    # Re-add as a pair entry using a helper approach:
                    # Since HiddenMemberProvider doesn't natively support pairs,
                    # add key and value as separate indexed children with clear names
                    pass

                found += 1

        # Simpler fallback: just add key children, user can expand to see structure
        # Reset and redo with cleaner approach
        self._members = []
        self._num_children = 0
        found = 0

        for span_idx in range(num_spans):
            if found >= cap:
                break
            span = spans.GetChildAtIndex(span_idx, lldb.eDynamicCanRunTarget, True)
            if not span.IsValid():
                continue
            offsets_member = span.GetChildMemberWithName('offsets')
            entries_member = span.GetChildMemberWithName('entries')
            if not offsets_member.IsValid() or not entries_member.IsValid():
                continue

            offsets_addr = offsets_member.GetValueAsUnsigned(0) if offsets_member.TypeIsPointerType() \
                           else offsets_member.GetLoadAddress()
            entries_addr = entries_member.GetValueAsUnsigned(0) if entries_member.TypeIsPointerType() \
                           else entries_member.GetLoadAddress()

            error = lldb.SBError()
            offsets_raw = process.ReadMemory(offsets_addr, QHASH_SPAN_ENTRIES, error)
            if not error.Success():
                continue

            for slot in range(QHASH_SPAN_ENTRIES):
                if found >= cap:
                    break
                offset = offsets_raw[slot] if isinstance(offsets_raw[slot], int) \
                         else ord(offsets_raw[slot])
                if offset == QHASH_EMPTY_OFFSET:
                    continue

                entry_addr = entries_addr + offset * node_size

                key_var = self.valobj.CreateValueFromAddress(
                    '[{}].key'.format(found), entry_addr, self._key_type)
                val_var = self.valobj.CreateValueFromAddress(
                    '[{}].value'.format(found), entry_addr + val_offset_in_node, self._val_type)

                if key_var.IsValid():
                    self._addChild(key_var)
                if val_var.IsValid():
                    self._addChild(val_var)

                self._num_children += 1
                found += 1


class QMultiHashFormatter(QHashFormatter):
    """LLDB synthetic provider for QMultiHash<K,V> (Qt6)"""

    def __init__(self, valobj, internal_dict):
        super(QMultiHashFormatter, self).__init__(valobj, internal_dict)


# ---------------------------------------------------------------------------
# QSet<T>  (Qt6) — backed by QHash<T, QHashDummyValue>
# ---------------------------------------------------------------------------

class QSetFormatter(HiddenMemberProvider):
    """LLDB synthetic provider for QSet<T> (Qt6)"""

    def __init__(self, valobj, internal_dict):
        super(QSetFormatter, self).__init__(valobj, internal_dict)
        self._item_type = valobj.GetType().GetTemplateArgumentType(0)

    def _update(self):
        # QSet<T> contains q_hash which is QHash<T, QHashDummyValue>
        q_hash = self.valobj.GetChildMemberWithName('q_hash')
        if not q_hash.IsValid():
            return

        d = q_hash.GetChildMemberWithName('d')
        if not d.IsValid() or d.GetValueAsUnsigned(0) == 0:
            return

        d_deref = d.Dereference()
        size = d_deref.GetChildMemberWithName('size').GetValueAsSigned(0)
        if size <= 0:
            return

        num_buckets = d_deref.GetChildMemberWithName('numBuckets').GetValueAsUnsigned(0)
        spans = d_deref.GetChildMemberWithName('spans')
        if not spans.IsValid() or num_buckets == 0:
            return

        key_size = self._item_type.GetByteSize()
        num_spans = (num_buckets + QHASH_SPAN_ENTRIES - 1) // QHASH_SPAN_ENTRIES
        process = self.valobj.GetProcess()
        cap = self._capping_size()
        found = 0

        for span_idx in range(num_spans):
            if found >= cap:
                break
            span = spans.GetChildAtIndex(span_idx, lldb.eDynamicCanRunTarget, True)
            if not span.IsValid():
                continue
            offsets_member = span.GetChildMemberWithName('offsets')
            entries_member = span.GetChildMemberWithName('entries')
            if not offsets_member.IsValid() or not entries_member.IsValid():
                continue

            offsets_addr = offsets_member.GetValueAsUnsigned(0) if offsets_member.TypeIsPointerType() \
                           else offsets_member.GetLoadAddress()
            entries_addr = entries_member.GetValueAsUnsigned(0) if entries_member.TypeIsPointerType() \
                           else entries_member.GetLoadAddress()

            error = lldb.SBError()
            offsets_raw = process.ReadMemory(offsets_addr, QHASH_SPAN_ENTRIES, error)
            if not error.Success():
                continue

            for slot in range(QHASH_SPAN_ENTRIES):
                if found >= cap:
                    break
                offset = offsets_raw[slot] if isinstance(offsets_raw[slot], int) \
                         else ord(offsets_raw[slot])
                if offset == QHASH_EMPTY_OFFSET:
                    continue

                entry_addr = entries_addr + offset * key_size
                var = self.valobj.CreateValueFromAddress(
                    '[{}]'.format(found), entry_addr, self._item_type)
                if var.IsValid():
                    self._addChild(var)
                    found += 1

        self._num_children = found


# ---------------------------------------------------------------------------
# QDate / QTime / QDateTime — largely unchanged from Qt5 version,
# but QDateTime in Qt6 stores data inline (packed pointer trick).
# We use toSecsSinceEpoch() via invoke() which still works.
# ---------------------------------------------------------------------------

class QDateFormatter(HiddenMemberProvider):
    """LLDB synthetic provider for QDate (Qt6)"""

    def __init__(self, valobj, internal_dict):
        super(QDateFormatter, self).__init__(valobj, internal_dict)
        self._add_original = False

    def has_children(self):
        return True

    @staticmethod
    def parse(julian_day):
        if julian_day == 0:
            return None
        try:
            if julian_day >= 2299161:
                ell = julian_day + 68569
                n = (4 * ell) // 146097
                ell = ell - (146097 * n + 3) // 4
                i = (4000 * (ell + 1)) // 1461001
                ell = ell - (1461 * i) // 4 + 31
                j = (80 * ell) // 2447
                d = ell - (2447 * j) // 80
                ell = j // 11
                m = j + 2 - (12 * ell)
                y = 100 * (n - 49) + i + ell
            else:
                julian_day += 32082
                dd = (4 * julian_day + 3) // 1461
                ee = julian_day - (1461 * dd) // 4
                mm = ((5 * ee) + 2) // 153
                d = ee - (153 * mm + 2) // 5 + 1
                m = mm + 3 - 12 * (mm // 10)
                y = dd - 4800 + (mm // 10)
                if y <= 0:
                    return None
            return dt.date(int(y), int(m), int(d))
        except Exception:
            return None

    def _update(self):
        jd = self.valobj.GetChildMemberWithName('jd')
        self._addChild(jd)
        pydate = self.parse(jd.GetValueAsUnsigned(0))
        if pydate is None:
            return
        self._addChild(('(ISO)', pydate.isoformat()))
        try:
            locale_str = pydate.strftime('%x')
            self._addChild(('(Locale)', locale_str))
        except Exception:
            pass


def QDateSummaryProvider(valobj, internal_dict):
    if valobj.IsValid():
        content = valobj.GetChildMemberWithName('(Locale)')
        if content.IsValid():
            summary = content.GetSummary()
            if summary:
                return summary
        pydate = QDateFormatter.parse(
            valobj.GetChildMemberWithName('jd').GetValueAsUnsigned(0))
        if pydate is not None:
            return pydate.isoformat()
    return '<Invalid>'


class QTimeFormatter(HiddenMemberProvider):
    """LLDB synthetic provider for QTime (Qt6)"""

    def __init__(self, valobj, internal_dict):
        super(QTimeFormatter, self).__init__(valobj, internal_dict)
        self._add_original = False

    def has_children(self):
        return True

    @staticmethod
    def parse(ms):
        if ms < 0:
            return None
        try:
            hour = ms // 3600000
            minute = (ms % 3600000) // 60000
            second = (ms // 1000) % 60
            msec = ms % 1000
            return dt.time(int(hour), int(minute), int(second), int(msec) * 1000)
        except Exception:
            return None

    def _update(self):
        mds = self.valobj.GetChildMemberWithName('mds')
        self._addChild(mds)
        pytime = self.parse(mds.GetValueAsUnsigned(0))
        if pytime is None:
            return
        self._addChild(('(ISO)', pytime.strftime('%H:%M:%S.') + '{:03d}'.format(pytime.microsecond // 1000)))
        try:
            self._addChild(('(Locale)', pytime.strftime('%X')))
        except Exception:
            pass


def QTimeSummaryProvider(valobj, internal_dict):
    if valobj.IsValid():
        content = valobj.GetChildMemberWithName('(Locale)')
        if content.IsValid():
            summary = content.GetSummary()
            if summary:
                return summary
        pytime = QTimeFormatter.parse(
            valobj.GetChildMemberWithName('mds').GetValueAsUnsigned(0))
        if pytime is not None:
            return pytime.strftime('%H:%M:%S')
    return None


class QDateTimeFormatter(HiddenMemberProvider):
    """LLDB synthetic provider for QDateTime (Qt6)"""

    def __init__(self, valobj, internal_dict):
        super(QDateTimeFormatter, self).__init__(valobj, internal_dict)

    def has_children(self):
        return True

    @staticmethod
    def getdata(var):
        # Qt6 stores data inline in a packed pointer; use the public API via invoke
        return invoke(var, 'toSecsSinceEpoch')

    @staticmethod
    def parse(time_t, utc=False):
        if time_t is None:
            return None
        try:
            totuple = time.gmtime if utc else time.localtime
            return totuple(time_t)
        except Exception:
            return None

    def _update(self):
        time_t = self.getdata(self.valobj)
        if not time_t.IsValid():
            return

        self._addChild(rename('toSecsSinceEpoch', time_t))

        secs = time_t.GetValueAsUnsigned(0)
        local_tt = self.parse(secs)
        utc_tt = self.parse(secs, utc=True)

        if utc_tt:
            try:
                formatted = time.strftime('%Y-%m-%d %H:%M:%S', utc_tt)
                self._addChild(('(ISO)', formatted))
            except Exception:
                pass

        if local_tt:
            try:
                self._addChild(('(Locale)', time.strftime('%c', local_tt)))
            except Exception:
                pass

        if utc_tt:
            try:
                self._addChild(('(UTC)', time.strftime('%c', utc_tt)))
            except Exception:
                pass


def QDateTimeSummaryProvider(valobj, internal_dict):
    if valobj.IsValid():
        content = valobj.GetChildMemberWithName('(Locale)')
        if content.IsValid():
            summary = content.GetSummary()
            if summary:
                return summary
        secs = QDateTimeFormatter.getdata(valobj).GetValueAsUnsigned(0)
        tt = QDateTimeFormatter.parse(secs)
        if tt:
            return time.strftime('%Y-%m-%d %H:%M:%S', tt)
    return None


# ---------------------------------------------------------------------------
# QUrl — Qt6 still uses QUrlPrivate with named QString members,
# so the Qt5 struct-member approach works. Keeping it simple.
# ---------------------------------------------------------------------------

class QUrlFormatter(HiddenMemberProvider):
    """LLDB synthetic provider for QUrl (Qt6)"""

    def __init__(self, valobj, internal_dict):
        super(QUrlFormatter, self).__init__(valobj, internal_dict)
        target = valobj.GetTarget()
        self._int_type = target.GetBasicType(lldb.eBasicTypeInt)
        self._qstring_type = target.FindFirstType('QString')

    def _update(self):
        d = self.valobj.GetChildMemberWithName('d')
        if not d.IsValid() or d.GetValueAsUnsigned(0) == 0:
            return

        dataobj = d.Dereference()
        if not dataobj.IsValid():
            return

        # Try to access named members (requires Qt debug symbols)
        scheme = dataobj.GetChildMemberWithName('scheme')
        if scheme.IsValid():
            host = dataobj.GetChildMemberWithName('host')
            path = dataobj.GetChildMemberWithName('path')
            username = dataobj.GetChildMemberWithName('userName')
            password = dataobj.GetChildMemberWithName('password')
            query = dataobj.GetChildMemberWithName('query')
            fragment = dataobj.GetChildMemberWithName('fragment')
            port_member = dataobj.GetChildMemberWithName('port')

            scheme_str = printableQString(scheme)[0] or ''
            host_str = printableQString(host)[0] or ''
            path_str = printableQString(path)[0] or ''

            url = '{}://{}{}'.format(scheme_str, host_str, path_str) if scheme_str else path_str
            self._addChild(('(encoded)', url), hidden=True)
            self._addChild(port_member)
            self._addChild(scheme)
            self._addChild(username)
            self._addChild(password)
            self._addChild(host)
            self._addChild(path)
            self._addChild(query)
            self._addChild(fragment)
            return

        # Fallback: invoke toString()
        res = invoke(self.valobj, 'toString', '(QUrl::FormattingOptions)0')
        if res.IsValid():
            self._addChild(rename('(encoded)', res), hidden=True)
        else:
            self._add_original = False
            self._addChild(self.valobj.GetChildMemberWithName('d'))


def QUrlSummaryProvider(valobj, internal_dict):
    if valobj.IsValid():
        content = valobj.GetChildMemberWithName('(encoded)')
        if content.IsValid():
            summary = content.GetSummary()
            if summary:
                return summary
    return None


# ---------------------------------------------------------------------------
# QUuid — unchanged, layout is stable across Qt versions
# ---------------------------------------------------------------------------

class QUuidFormatter(HiddenMemberProvider):
    """LLDB synthetic provider for QUuid"""

    def __init__(self, valobj, internal_dict):
        super(QUuidFormatter, self).__init__(valobj, internal_dict)

    def has_children(self):
        return False


def QUuidSummaryProvider(valobj, internal_dict):
    data = [valobj.GetChildMemberWithName(name).GetValueAsUnsigned(0)
            for name in ['data1', 'data2', 'data3']]
    data += [val.GetValueAsUnsigned(0) for val in valobj.GetChildMemberWithName('data4')]
    return '{{{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}}}'.format(*data)
