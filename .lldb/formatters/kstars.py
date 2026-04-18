"""
LLDB data formatters for KStars types

SPDX-FileCopyrightText: 2026 KStars Team
SPDX-License-Identifier: GPL-2.0-or-later
"""


def __lldb_init_module(debugger, unused):
    """Register KStars type formatters with LLDB"""
    debugger.HandleCommand('type summary add dms -w kstars -F kstars.DmsSummaryProvider')
    debugger.HandleCommand('type summary add CachingDms -w kstars -F kstars.CachingDmsSummaryProvider')
    debugger.HandleCommand('type summary add SkyPoint -w kstars -F kstars.SkyPointSummaryProvider')
    debugger.HandleCommand('type summary add SkyObject -w kstars -F kstars.SkyObjectSummaryProvider')
    debugger.HandleCommand('type summary add GeoLocation -w kstars -F kstars.GeoLocationSummaryProvider')
    
    debugger.HandleCommand('type category enable kstars')


def DmsSummaryProvider(valobj, internal_dict):
    """Format dms (degrees/minutes/seconds) angle"""
    try:
        # Get the D member which stores the angle in degrees
        degrees = valobj.GetChildMemberWithName('D').GetValueAsUnsigned(0)
        
        # Handle invalid values
        if degrees == 0:
            degrees_float = valobj.GetChildMemberWithName('D').GetValue()
            if degrees_float and 'nan' in degrees_float.lower():
                return '<invalid>'
        
        # Convert to float
        degrees_val = valobj.GetChildMemberWithName('D').GetValueAsUnsigned(0)
        # Try to get as double
        error = valobj.GetError()
        if error.Success():
            degrees_double = valobj.GetChildMemberWithName('D').GetValueAsUnsigned(0)
            # This is a simplified version - just show degrees
            degrees_str = valobj.GetChildMemberWithName('D').GetValue()
            return f'{degrees_str}°'
        
        return '<unknown>'
    except:
        return '<error>'


def CachingDmsSummaryProvider(valobj, internal_dict):
    """Format CachingDms (inherits from dms)"""
    # CachingDms inherits from dms, so we can use the same formatting
    return DmsSummaryProvider(valobj, internal_dict)


def SkyPointSummaryProvider(valobj, internal_dict):
    """Format SkyPoint (celestial coordinates)"""
    try:
        # Get RA and Dec members
        ra = valobj.GetChildMemberWithName('RA')
        dec = valobj.GetChildMemberWithName('Dec')
        
        if ra.IsValid() and dec.IsValid():
            ra_val = ra.GetChildMemberWithName('D').GetValue()
            dec_val = dec.GetChildMemberWithName('D').GetValue()
            
            if ra_val and dec_val:
                # RA is stored in degrees but typically displayed in hours
                return f'RA: {ra_val}° Dec: {dec_val}°'
        
        return '<invalid coordinates>'
    except:
        return '<error>'


def SkyObjectSummaryProvider(valobj, internal_dict):
    """Format SkyObject (astronomical object with name and position)"""
    try:
        # Try to get the object name
        name_member = valobj.GetChildMemberWithName('Name')
        if name_member.IsValid():
            name = name_member.GetSummary()
            if name:
                # Remove quotes if present
                name = name.strip('"')
                
                # Also get coordinates from base SkyPoint class
                coords = SkyPointSummaryProvider(valobj, internal_dict)
                if coords and coords != '<invalid coordinates>':
                    return f'{name} ({coords})'
                else:
                    return f'{name}'
        
        return '<unnamed object>'
    except:
        return '<error>'


def GeoLocationSummaryProvider(valobj, internal_dict):
    """Format GeoLocation (geographic location on Earth)"""
    try:
        # Get location name
        name_member = valobj.GetChildMemberWithName('Name')
        lat_member = valobj.GetChildMemberWithName('Latitude')
        lng_member = valobj.GetChildMemberWithName('Longitude')
        
        name = None
        if name_member.IsValid():
            name = name_member.GetSummary()
            if name:
                name = name.strip('"')
        
        # Try to get lat/lon
        if lat_member.IsValid() and lng_member.IsValid():
            lat_val = lat_member.GetChildMemberWithName('D').GetValue() if lat_member.GetChildMemberWithName('D').IsValid() else None
            lng_val = lng_member.GetChildMemberWithName('D').GetValue() if lng_member.GetChildMemberWithName('D').IsValid() else None
            
            if name:
                if lat_val and lng_val:
                    return f'{name} (Lat: {lat_val}°, Lon: {lng_val}°)'
                else:
                    return f'{name}'
            elif lat_val and lng_val:
                return f'Lat: {lat_val}°, Lon: {lng_val}°'
        
        if name:
            return f'{name}'
        
        return '<unknown location>'
    except:
        return '<error>'
