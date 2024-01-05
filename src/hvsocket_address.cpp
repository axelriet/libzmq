/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "hvsocket_address.hpp"

#if defined(ZMQ_HAVE_HVSOCKET)

#ifdef ZMQ_HAVE_WINDOWS
#include <ComputeCore.h>
#pragma comment(lib, "delayimp")
#pragma comment(lib, "computecore")
const wchar_t GuestCommunicationServicesKeyPath[] =
  L"SOFTWARE\\Microsoft\\Windows "
  L"NT\\CurrentVersion\\Virtualization\\GuestCommunicationServices";
const char ElementName[] = "ElementName";
#endif

#include <climits>
#include <string>
#include <sstream>
#include <iomanip>

#include "err.hpp"

//
// TinyJson by Rafa Garc�a (https://github.com/rafagafe/tiny-json)
//
// Included below is a fork modified to handle
// UTF-16 directly as this is what HCS returns.
//
// https://github.com/axelriet/tiny-json.git
//

#define TINY_JSON_USE_WCHAR

#include "..\external\tiny-json\tiny-json.h"
#include "..\external\tiny-json\tiny-json.c"

zmq::hvsocket_address_t::hvsocket_address_t ()
{
    memset (&address, 0, sizeof address);
}

zmq::hvsocket_address_t::hvsocket_address_t (ctx_t *parent_) : parent (parent_)
{
    memset (&address, 0, sizeof address);
}

zmq::hvsocket_address_t::hvsocket_address_t (const sockaddr *sa,
                                             socklen_t sa_len,
                                             ctx_t *parent_) :
    parent (parent_)
{
    zmq_assert (sa && sa_len > 0);

    memset (&address, 0, sizeof (address));

    if (sa->sa_family == parent->get_hvsocket_socket_family ()) {
        zmq_assert (sa_len <= sizeof (address));
        memcpy (&address, sa, sa_len);
    }
}

static bool GuidFromStringW (_In_z_ const wchar_t *str, _Out_ GUID *guid)
{
    //
    // "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
    //

    *guid = GUID_NULL;

    const size_t len = wcslen (str);

    if (len != 36) {
        return false;
    }

    //
    // Validate the string format
    //

    for (int i = 0; i < len; ++i) {
        const wchar_t g = str[i];
        if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) {
            if (g != L'-') {
                return false;
            }
        } else if (!std::isxdigit (g)) {
            return false;
        }
    }

    wchar_t *pEnd;

    guid->Data1 = wcstoul (str, &pEnd, 16);
    guid->Data2 = (unsigned short) wcstoul (str + 9, &pEnd, 16);
    guid->Data3 = (unsigned short) wcstoul (str + 14, &pEnd, 16);

    wchar_t b[3]{};

    b[0] = str[19];
    b[1] = str[20];

    guid->Data4[0] = (unsigned char) wcstoul (b, &pEnd, 16);

    b[0] = str[21];
    b[1] = str[22];

    guid->Data4[1] = (unsigned char) wcstoul (b, &pEnd, 16);

    for (int i = 0; i < 6; ++i) {
        memcpy (b, str + 24 + i * 2, 2 * sizeof (b[0]));
        guid->Data4[2 + i] = (unsigned char) wcstoul (b, &pEnd, 16);
    }

    return true;
}

static bool GuidFromStringA (_In_z_ const char *str, _Out_ GUID *guid)
{
    //
    // "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
    //

    *guid = GUID_NULL;

    const size_t len = strlen (str);

    if (len != 36) {
        return false;
    }

    //
    // Validate the string format
    //

    for (int i = 0; i < len; ++i) {
        const char g = str[i];
        if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) {
            if (g != '-') {
                return false;
            }
        } else if (!std::isxdigit (g)) {
            return false;
        }
    }

    char *pEnd;

    guid->Data1 = strtoul (str, &pEnd, 16);
    guid->Data2 = (unsigned short) strtoul (str + 9, &pEnd, 16);
    guid->Data3 = (unsigned short) strtoul (str + 14, &pEnd, 16);

    char b[3]{};

    b[0] = str[19];
    b[1] = str[20];

    guid->Data4[0] = (unsigned char) strtoul (b, &pEnd, 16);

    b[0] = str[21];
    b[1] = str[22];

    guid->Data4[1] = (unsigned char) strtoul (b, &pEnd, 16);

    for (int i = 0; i < 6; ++i) {
        memcpy (b, str + 24 + i * 2, 2 * sizeof (b[0]));
        guid->Data4[2 + i] = (unsigned char) strtoul (b, &pEnd, 16);
    }

    return true;
}

std::ostream &operator<< (std::ostream &os, REFGUID guid)
{
    os << std::uppercase;

    os << std::hex << std::setfill ('0') << std::setw (8) << guid.Data1 << '-';

    os << std::hex << std::setfill ('0') << std::setw (4) << guid.Data2 << '-';

    os << std::hex << std::setfill ('0') << std::setw (4) << guid.Data3 << '-';

    os << std::hex << std::setfill ('0') << std::setw (2)
       << static_cast<short> (guid.Data4[0]);

    os << std::hex << std::setfill ('0') << std::setw (2)
       << static_cast<short> (guid.Data4[1]) << '-';

    os << std::hex << std::setfill ('0') << std::setw (2)
       << static_cast<short> (guid.Data4[2]);

    os << std::hex << std::setfill ('0') << std::setw (2)
       << static_cast<short> (guid.Data4[3]);

    os << std::hex << std::setfill ('0') << std::setw (2)
       << static_cast<short> (guid.Data4[4]);

    os << std::hex << std::setfill ('0') << std::setw (2)
       << static_cast<short> (guid.Data4[5]);

    os << std::hex << std::setfill ('0') << std::setw (2)
       << static_cast<short> (guid.Data4[6]);

    os << std::hex << std::setfill ('0') << std::setw (2)
       << static_cast<short> (guid.Data4[7]);

    os << std::nouppercase;

    return os;
}

#ifdef ZMQ_HAVE_WINDOWS
static std::wstring ToUtf16 (_In_z_ const char *str)
{
    std::wstring retVal;

    if (str && *str) {
        const int cchNeeded =
          MultiByteToWideChar (CP_UTF8, 0, str, -1, NULL, 0);
        if (cchNeeded > 0) {
            retVal.resize (cchNeeded);
            MultiByteToWideChar (CP_UTF8, 0, str, -1, &retVal[0], cchNeeded);
        }
    }

    return retVal;
}

static bool GetComputeSystemIdFromNameOrIndex (_In_z_ const char *nameOrIndex,
                                               _Out_ GUID *guid)
{
    bool retVal{};
    PWSTR result{};
    json_t buf[64]{};
    unsigned long i{};
    char *end{nullptr};
    bool indexLookup{};
    GUID indexMatchGuid{};
    const char *begin{nameOrIndex};
    std::wstring nameOrIndexW = ToUtf16 (nameOrIndex);
    const unsigned long index{strtoul (begin, &end, 10)};

    *guid = GUID_NULL;

    HCS_OPERATION op = HcsCreateOperation (nullptr, nullptr);

    if (!op) {
        goto cleanup;
    }

    if (FAILED (HcsEnumerateComputeSystems (nullptr, op))) {
        goto cleanup;
    }

    if (FAILED (HcsGetOperationResult (op, &result))) {
        goto cleanup;
    }

    //
    // Optional now, but we no longer need the operation.
    //

    HcsCloseOperation (op);
    op = nullptr;

    // The Json looks like this (an array of objects)
    //
    // [{
    //     "Id" : "AF5F35E3-FD7A-4573-9449-E47223939979",
    //     "SystemType" : "VirtualMachine",
    //     "Name" : "WinDev2311Eval",
    //     "Owner" : "VMMS",
    //     "RuntimeId" : "af5f35e3-fd7a-4573-9449-e47223939979"
    // }]
    //
    // We only care about "Id" and "Name" and sometimes
    // there is no Name, so we must skip those entries.
    //

    {
        const json_t *json = json_create (result, buf, _countof (buf));

        if (!json) {
            goto cleanup;
        }

        //
        // Minimally validate that we got what we expected.
        //

        if (json_getType (json) != JSON_ARRAY) {
            goto cleanup;
        }


        if (end != begin && !*end) {
            //
            // The whole "name" is a number, so it may be an index. There
            // is a chance that someone named their containers/vms with a
            // number. To avoid mistaking a vm/container name's that is a
            // number we iterate the whole list and compare the names and
            // ensure that name matching takes precedence over index.
            //

            indexLookup = true;
        }

        for (json_t const *entry{json_getChild (json)}; entry != nullptr;
             entry = json_getSibling (entry), i++) {
            //
            // Minimally validate again that we got what we expected.
            //

            if (json_getType (entry) != JSON_OBJ) {
                goto cleanup;
            }

            //
            // Some entries don't have a name and must be skipped, unless
            // we are doing an index lookup.
            //

            auto nameValue = json_getPropertyValue (entry, L"Name");

            const bool indexMatch = (indexLookup && (i == index));

            const bool nameMatch =
              (nameValue && !_wcsicmp (nameValue, nameOrIndexW.c_str ()));

            if (indexMatch || nameMatch) {
                //
                // Retrieve the Id property and convert it to a GUID.
                //

                auto idValue = json_getPropertyValue (entry, L"Id");

                GUID tempGuid{};

                if (!idValue || !GuidFromStringW (idValue, &tempGuid)) {
                    goto cleanup;
                }

                if (nameMatch) {
                    //
                    // Name match gets the prize, we are done.
                    //

                    *guid = tempGuid;
                    retVal = true;
                    goto cleanup;
                }

                if (indexMatch) {
                    //
                    // Index match. Store it and continue.
                    //

                    indexMatchGuid = tempGuid;
                }
            }
        }

        //
        // If we get there then there was no name matching. If
        // they passed a name that looks like an indice and we
        // stored an index match, return that.
        //

        if (indexLookup && (indexMatchGuid != GUID_NULL)) {
            *guid = indexMatchGuid;
            retVal = true;
        }
    }

cleanup:

    if (op) {
        HcsCloseOperation (op);
        op = nullptr;
    }

    if (result) {
        LocalFree (result);
        result = nullptr;
    }

    return retVal;
}

static bool GetHvSocketServiceIdFromName (_In_z_ const char *name,
                                          _Out_ GUID *guid)
{
    HKEY hKey{};
    bool retVal{};
    DWORD dwIndex{};
    LSTATUS status{};
    char subkeyName[37]{};
    const size_t nameLength{strlen (name)};
    auto valueName{std::make_unique<char[]> (nameLength + 1)};

    *guid = GUID_NULL;

    if (!nameLength || !valueName) {
        //
        // The name cannot be empty.
        //

        goto cleanup;
    }

    status =
      RegOpenKeyExW (HKEY_LOCAL_MACHINE, GuestCommunicationServicesKeyPath, 0,
                     KEY_READ, &hKey);

    if (status != ERROR_SUCCESS) {
        //
        // Maybe there is no Hyper-V on this machine.
        //

        goto cleanup;
    }

    //
    // Loop until the end. The buffer exactly accomodates a
    // GUID in string format without the braces, which is
    // the format used there, so we ignore ERROR_MORE_DATA.
    //

    while ((status =
              RegEnumKeyA (hKey, dwIndex++, subkeyName, _countof (subkeyName)))
           != ERROR_NO_MORE_ITEMS) {
        if (status != ERROR_SUCCESS) {
            //
            // We skip the keys that are too long.
            //

            zmq_assert (status == ERROR_MORE_DATA);
            continue;
        }

        DWORD valueNameSize{static_cast<DWORD> (nameLength + 1)};
        status = RegGetValueA (hKey, subkeyName, ElementName, RRF_RT_REG_SZ,
                               nullptr, valueName.get (), &valueNameSize);

        if (status != ERROR_SUCCESS) {
            //
            // If the value is too large or missing, or not a string,
            // we skip the key. In particular we don't needlessly
            // string-compare values that are too long.
            //

            zmq_assert ((status == ERROR_MORE_DATA)
                        || (status == ERROR_FILE_NOT_FOUND)
                        || (status == ERROR_UNSUPPORTED_TYPE));
            continue;
        }

        if (_stricmp (valueName.get (), name) == 0) {
            //
            // The key name is the service id.
            //

            if (!GuidFromStringA (subkeyName, guid)) {
                goto cleanup;
            }

            //
            // Done.
            //

            retVal = true;
            break;
        }
    }

cleanup:

    if (hKey) {
        RegCloseKey (hKey);
        hKey = nullptr;
    }

    return retVal;
}

//
// Attempts to register the HvSocket Service Id in the registry.
//

static bool RegisterNamedHvSocketServiceId (_In_z_ const char *name,
                                            _In_ REFGUID guid,
                                            _In_ BOOL volatileKey)
{
    HKEY hKey{};
    bool retVal{};
    HKEY hSubKey{};
    LSTATUS status{};
    std::stringstream s;
    std::string subKeyName;

    status =
      RegOpenKeyExW (HKEY_LOCAL_MACHINE, GuestCommunicationServicesKeyPath, 0,
                     KEY_WRITE, &hKey);

    if (status != ERROR_SUCCESS) {
        //
        // Maybe there is no Hyper-V on this machine.
        //

        goto cleanup;
    }

    //
    // See if the service id subkey is already registered.
    //

    s << guid;
    subKeyName = s.str ();

    status = RegOpenKeyExA (hKey, subKeyName.c_str (), 0, KEY_READ, &hSubKey);

    if (status == ERROR_SUCCESS) {
        //
        // It is, bail out.
        //

        retVal = true;
        goto cleanup;
    }

    status = RegCreateKeyExA (hKey, subKeyName.c_str (), 0, nullptr,
                              volatileKey ? REG_OPTION_VOLATILE
                                          : REG_OPTION_NON_VOLATILE,
                              KEY_ALL_ACCESS, nullptr, &hSubKey, nullptr);

    if (status != ERROR_SUCCESS) {
        //
        // Cannot create the subkey.
        //

        goto cleanup;
    }

    status =
      RegSetValueExA (hSubKey, ElementName, 0, REG_SZ,
                      (const unsigned char *) name, (DWORD) strlen (name) + 1);

    if (status != ERROR_SUCCESS) {
        //
        // Cannot write the value.
        //

        goto cleanup;
    }

    //
    // There might be a race between us registering a new service id
    // and them noticing that we did. Flush the regkey and yield the
    // current timeslice just in case, so their registry watcher has
    // a chance to notice the change (assuming they have one).
    //

    RegFlushKey (hSubKey);
    RegFlushKey (hKey);
    Sleep (0);

    retVal = true;

cleanup:

    if (hSubKey) {
        RegCloseKey (hSubKey);
        hSubKey = nullptr;
    }

    if (hKey) {
        RegCloseKey (hKey);
        hKey = nullptr;
    }

    return retVal;
}

#endif

int zmq::hvsocket_address_t::resolve (const char *path_)
{
#ifndef NDEBUG
    // TODO: Move this into a test?
    GUID guid1{};
    zmq_assert (
      GuidFromStringA ("C0B6B7FC-0D90-4812-A606-9E8E13709825", &guid1));
    std::stringstream s1;
    s1 << guid1;
    zmq_assert (s1.str () == "C0B6B7FC-0D90-4812-A606-9E8E13709825");
    GUID guid2{};
    zmq_assert (
      GuidFromStringW (L"C0B6B7FC-0D90-4812-A606-9E8E13709825", &guid2));
    std::stringstream s2;
    s2 << guid2;
    zmq_assert (s2.str () == "C0B6B7FC-0D90-4812-A606-9E8E13709825");
    zmq_assert (guid1 == guid2);
#endif

    memset (&address, 0, sizeof (address));

    //
    // Find the ':' at end that separates the VM ID from the Service ID.
    //

    const char *delimiter = strrchr (path_, ':');

    if (!delimiter) {
        errno = EINVAL;
        return -1;
    }

    //
    // Separate the vm id and service id.
    //

    std::string addr_str (path_, delimiter - path_);
    std::string port_str (delimiter + 1);

    address.VmId = HV_GUID_WILDCARD;
    address.ServiceId = HV_GUID_WILDCARD;

    if (!addr_str.length ()) {
        //
        // Address cannot be empty.
        //

        errno = EINVAL;
        return -1;

    } else if (addr_str != "*") {
        //
        // Try GUID conversion first
        //

        if (!GuidFromStringA (addr_str.c_str (), &address.VmId)) {
#ifdef ZMQ_HAVE_WINDOWS
            //
            // Not a GUID. Try resolving the string as a vm/container
            // name or index.
            //

            if (!GetComputeSystemIdFromNameOrIndex (addr_str.c_str (),
                                                    &address.VmId)) {
                //
                // Name/index resolution failed. Try aliases last.
                //
#endif
                if (addr_str == "any") {
                    address.VmId = HV_GUID_WILDCARD;
                } else if (addr_str == "children") {
                    address.VmId = HV_GUID_CHILDREN;
                } else if ((addr_str == "local") || (addr_str == "localhost")
                           || (addr_str == "loopback")) {
                    address.VmId = HV_GUID_LOOPBACK;
                } else if (addr_str == "parent") {
                    address.VmId = HV_GUID_PARENT;
                } else if (addr_str == "silohost") {
                    address.VmId = HV_GUID_SILOHOST;
                } else {
                    //
                    // We are having a bad day.
                    //

                    errno = EINVAL;
                    return -1;
                }
#ifdef ZMQ_HAVE_WINDOWS
            }
#endif
        }
    }

    if (!port_str.length ()) {
        //
        // Port cannot be empty.
        //

        errno = EINVAL;
        return -1;

    } else if (port_str != "*") {
        //
        // Try guid conversion first
        //

        if (!GuidFromStringA (port_str.c_str (), &address.ServiceId)) {
#ifdef ZMQ_HAVE_WINDOWS
            //
            // Try resolving the string as a registered service name first.
            //

            if (!GetHvSocketServiceIdFromName (port_str.c_str (),
                                               &address.ServiceId)) {
                //
                // Service name resolution failed. See if it's a numeric port.
                //
#endif
                char *end{nullptr};
                const char *begin{port_str.c_str ()};
                const unsigned long portNumber{strtoul (begin, &end, 10)};

                if (end != begin && !*end) {
                    //
                    // The whole "serviceId" is a number, so it must be
                    // a port number.
                    //

                    if (portNumber & 0x80000000) {
                        //
                        // Port numbers must be <= 0x7fffffff
                        //

                        errno = EINVAL;
                        return -1;
                    }

                    //
                    // It looks like a number that can be used as port number,
                    // stuff it into the VSOCK template. This franken-GUID is
                    // given special treatment by the underlying transport.
                    //

                    address.ServiceId = HV_GUID_VSOCK_TEMPLATE;
                    address.ServiceId.Data1 = portNumber;

#ifdef ZMQ_HAVE_WINDOWS
                    //
                    // On Windows, we auto-register the service id port number.
                    //

                    (void) RegisterNamedHvSocketServiceId (
                      port_str.c_str (), address.ServiceId, TRUE);
#endif
                } else {
                    //
                    // Port number resolution failed. Nothing worked.
                    //

                    errno = EINVAL;
                    return -1;
                }
#ifdef ZMQ_HAVE_WINDOWS
            }
        } else {
            //
            // On Windows, we auto-register the service id.
            //

            (void) RegisterNamedHvSocketServiceId (
              ("Service-" + port_str).c_str (), address.ServiceId, TRUE);
#endif
        }
    }

    address.Family =
      static_cast<unsigned short> (parent->get_hvsocket_socket_family ());

    return 0;
}

int zmq::hvsocket_address_t::to_string (std::string &addr_) const
{
    if (address.Family != parent->get_hvsocket_socket_family ()) {
        addr_.clear ();
        return -1;
    }

    std::stringstream s;

    s << protocol_name::hvsocket << "://";

    if (address.VmId == HV_GUID_WILDCARD) {
        s << "*";
    } else {
        s << address.VmId;
    }

    s << ":";

    if (address.ServiceId == HV_GUID_WILDCARD) {
        s << "*";
    } else {
        s << address.ServiceId;
    }

    addr_ = s.str ();

    return 0;
}

const sockaddr *zmq::hvsocket_address_t::addr () const
{
    return reinterpret_cast<const sockaddr *> (&address);
}

socklen_t zmq::hvsocket_address_t::addrlen () const
{
    return static_cast<socklen_t> (sizeof address);
}

#if defined ZMQ_HAVE_WINDOWS
unsigned short zmq::hvsocket_address_t::family () const
#else
sa_family_t zmq::hvsocket_address_t::family () const
#endif
{
    return AF_HYPERV;
}

#endif
