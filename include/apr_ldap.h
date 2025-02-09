/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file apr_ldap.h
 * @brief  APR-UTIL LDAP routines
 */
#ifndef APU_LDAP_H
#define APU_LDAP_H

/**
 * @defgroup APR_Util_LDAP LDAP routines
 *
 * The APR LDAP routines provide a common, cross platform, ability to connect
 * to and search an LDAP server.
 *
 * The goals of the API are:
 *
 * - Work within the functionality of APR pools. Requests from different pools
 *   can make LDAP requests of a common connection, and when the connection
 *   pool or the request pool goes away, the connection and/or LDAP requests
 *   are cleaned up gracefully.
 *
 * - Offer an asynchronous API that can be used for non blocking access to an
 *   LDAP server. The responses APR_WANT_READ and APR_WANT_WRITE make it clear
 *   whether the API wants to read or write to the LDAP server in the next API
 *   call.
 *
 * - Be as simple as possible. Data is returned fully processed in callbacks,
 *   removing the need for API calls to access data, and intermediate data
 *   structures.
 *
 * In typical use, the following calls are used:
 *
 * - apr_ldap_initialise() - create a handle to keep track of a connection.
 * - apr_ldap_option_set() - set the URL, or the socket descriptor for the
 *                           connextion to the server.
 * - apr_ldap_connect() - if an URL was specified, connect to the server and
 *                        confirm success.
 * - apr_ldap_bind() - initiate a bind, and specify a callback when done.
 *
 * Enter the event loop, where we do the following until the connection is
 * closed.
 *
 * - apr_ldap_process() - when writable, perform tasks that require writing to
 *                        the LDAP server.
 * - apr_ldap_result() - when readable, perform tasks that require reading from
 *                       the LDAP server.
 *
 * Respond appropriately to callbacks, lining up calls to apr_ldap_compare() and
 * apr_ldap_search() as needed.
 *
 * @ingroup APR
 * @{
 */

#include "apr.h"

/*
 * Handle the case when LDAP is enabled
 */
#if APR_HAS_LDAP || defined(DOXYGEN)

#include "apu.h"
#include "apr_poll.h"
#include "apr_pools.h"
#include "apr_network_io.h"
#include "apu_errno.h"
#include "apr_escape.h"
#include "apr_buffer.h"

/*
 * LDAP URL handling
 */

/**
 * Individual fields accessible within an LDAP URL.
 * @see apr_ldap_url_parse */
typedef struct apr_ldap_url_desc_t {
    /** Next URL in a sequence of URLs */
    struct  apr_ldap_url_desc_t  *lud_next;
    /** URL scheme (ldap, ldaps, ldapi) */
    char    *lud_scheme;
    /** Host part of the URL */
    char    *lud_host;
    /** Port, if appropriate */
    int     lud_port;
    /** Base distinguished name of the search */
    char    *lud_dn;
    /** Attributes to be returned during a search */
    char    **lud_attrs;
    /** Scope of the search */
    int     lud_scope;
    /** Search filter */
    char    *lud_filter;
    /** Extensions */
    char    **lud_exts;
    /** Critical extensions */
    int     lud_crit_exts;
} apr_ldap_url_desc_t;

#ifndef APR_LDAP_URL_SUCCESS
/**
 * URL was successfully parsed.
 * @see apr_ldap_url_parse()
 */
#define APR_LDAP_URL_SUCCESS          0x00
/**
 * Can't allocate memory space
 * @see apr_ldap_url_parse()
 */
#define APR_LDAP_URL_ERR_MEM          0x01
/** 
 * Parameter is bad
 * @see apr_ldap_url_parse()
 */
#define APR_LDAP_URL_ERR_PARAM        0x02
/**
 * URL doesn't begin with "ldap[si]://"
 * @see apr_ldap_url_parse()
 */
#define APR_LDAP_URL_ERR_BADSCHEME    0x03
/** 
 * URL is missing trailing ">"
 * @see apr_ldap_url_parse()
 */
#define APR_LDAP_URL_ERR_BADENCLOSURE 0x04
/** 
 * URL is bad
 * @see apr_ldap_url_parse()
 */
#define APR_LDAP_URL_ERR_BADURL       0x05
/** 
 * Host port is bad
 * @see apr_ldap_url_parse()
 */
#define APR_LDAP_URL_ERR_BADHOST      0x06
/** 
 * Bad (or missing) attributes
 * @see apr_ldap_url_parse()
 */
#define APR_LDAP_URL_ERR_BADATTRS     0x07
/** 
 * Scope string is invalid (or missing)
 * @see apr_ldap_url_parse()
 */
#define APR_LDAP_URL_ERR_BADSCOPE     0x08
/** 
 * Bad or missing filter
 * @see apr_ldap_url_parse()
 */
#define APR_LDAP_URL_ERR_BADFILTER    0x09
/** 
 * Bad or missing extensions
 * @see apr_ldap_url_parse()
 */
#define APR_LDAP_URL_ERR_BADEXTS      0x0a
#endif

/**
 * Is this URL an ldap url? ldap://
 * @param url The url to test
 */
APR_DECLARE(int) apr_ldap_is_ldap_url(const char *url);

/**
 * Is this URL an SSL ldap url? ldaps://
 * @param url The url to test
 */
APR_DECLARE(int) apr_ldap_is_ldaps_url(const char *url);

/**
 * Is this URL an ldap socket url? ldapi://
 * @param url The url to test
 */
APR_DECLARE(int) apr_ldap_is_ldapi_url(const char *url);

/**
 * Parse an LDAP URL.
 * @param pool The pool to use
 * @param url_in The URL to parse
 * @param ludpp The structure to return the exploded URL
 * @param result_err The result structure of the operation
 */
APR_DECLARE(int) apr_ldap_url_parse_ext(apr_pool_t *pool,
                                        const char *url_in,
                                        apr_ldap_url_desc_t **ludpp,
                                        apu_err_t **result_err);

/**
 * Parse an LDAP URL.
 * @param pool The pool to use
 * @param url_in The URL to parse
 * @param ludpp The structure to return the exploded URL
 * @param result_err The result structure of the operation
 */
APR_DECLARE(int) apr_ldap_url_parse(apr_pool_t *pool,
                                    const char *url_in,
                                    apr_ldap_url_desc_t **ludpp,
                                    apu_err_t **result_err);









/* These symbols are not actually exported in a DSO build, but mapped into
 * a private exported function array for apr_ldap_stub to bind dynamically.
 * Rename them appropriately to protect the global namespace.
 */
#if defined(APU_DSO_LDAP_BUILD)

#define apr_ldap_info apr__ldap_info
#define apr_ldap_initialise apr__ldap_initialise
#define apr_ldap_option_get apr__ldap_option_get
#define apr_ldap_option_set apr__ldap_option_set
#define apr_ldap_connect apr__ldap_connect
#define apr_ldap_prepare apr__ldap_prepare
#define apr_ldap_process apr__ldap_process
#define apr_ldap_result apr__ldap_result
#define apr_ldap_poll apr__ldap_poll
#define apr_ldap_bind apr__ldap_bind
#define apr_ldap_compare apr__ldap_compare
#define apr_ldap_search apr__ldap_search
#define apr_ldap_unbind apr__ldap_unbind

#define APU_DECLARE_LDAP(type) type
#else
/** @see APR_DECLARE */
#define APU_DECLARE_LDAP(type) APR_DECLARE(type)
#endif

/**
 * Opaque structure representing the LDAP driver.
 * @see apr_ldap_get_driver
 */
typedef struct apr_ldap_driver_t apr_ldap_driver_t;


/** apr_ldap_get_driver: get the driver struct for a name
 *
 * The LDAP driver is unique in that LDAP libraries are almost exclusively
 * derived from RFC1823 "The LDAP Application Program Interface".
 *
 * As a result, unlike other drivers for other subsystems in APR, two
 * different drivers cannot be loaded at once, as the underlying libraries
 * share common symbols with one another.
 *
 * For this reason we have exactly one driver available at a time.
 *
 * This function loads the library, and registers a cleanup with the pool
 * provided to unload the library.
 *
 * This function can be called multiple times by independent code, cleanups
 * are reference counted so the last pool cleanup unloads the library.
 *
 * Calling this function explicitly is optional, and would be done to have
 * complete control over the lifetime of the driver.
 *
 * If this function is not called explicitly, this function will be called
 * if needed before the apr_ldap_info(), apr_ldap_initialise(),
 * apr_ldap_option_get(), and apr_ldap_option_set() functions,
 * registering cleanups in the pools provided to those functions if needed.
 *
 *  @param pool (process) pool to register cleanup that will unload the
 *              library. Cleanup is reference counted so the driver is
 *              unloaded on last access.
 *  @param driver Pointer to driver struct. Can be NULL.
 *  @param err Human readable error messages
 *  @return APR_SUCCESS for success
 *  @return APR_ENOTIMPL for no driver (when DSO not enabled)
 *  @return APR_EDSOOPEN if DSO driver file can't be opened
 *  @return APR_ESYMNOTFOUND if the driver file doesn't contain a driver
 */
APR_DECLARE(apr_status_t) apr_ldap_get_driver(apr_pool_t *pool,
                                              const apr_ldap_driver_t **driver,
                                              apu_err_t *err)
                                              __attribute__((nonnull(1,3)));



/**
 * Opaque structure tracking the state of an LDAP connection.
 *
 * @see apr_ldap_initialise
 */
typedef struct apr_ldap_t apr_ldap_t;


/**
 * APR LDAP info function
 *
 * This function returns a string describing the LDAP toolkit
 * currently in use. The string is placed inside result_err->reason.
 * @param pool The pool to use
 * @param result_err The returned result
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_info(apr_pool_t *pool,
                                             apu_err_t **result_err)
                                             __attribute__((nonnull(1,2)));



/**
 * Ports used by LDAP.
 */
/** ldap:/// default LDAP port */
#define APR_LDAP_PORT 389
/** ldaps:/// default LDAP over TLS port */
#define APR_LDAPS_PORT 636


/**
 * APR LDAP initialise function
 *
 * This function is responsible for initialising an LDAP
 * connection in a toolkit independant way. It does the
 * job of ldap_initialize() from the C api.
 *
 * The setting of the LDAP server to connect is made after
 * this function returns, using the apr_ldap_option_set()
 * call with APR_LDAP_OPT_DESC or APR_LDAP_OPT_URI.
 *
 * A cleanup for the connection is registered in the given pool.
 *
 * @param pool The pool to use
 * @param ldap The ldap context returned
 * @param err On error, error details are written to the
 *        structure.
 * @see apr_ldap_option_set
 * @see APR_LDAP_OPT_DESC
 * @see APR_LDAP_OPT_URI
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_initialise(apr_pool_t *pool,
                                                   apr_ldap_t **ldap,
                                                   apu_err_t *err)
                                                   __attribute__((nonnull(1,2,3)));


/*
 * LDAP options.
 */

/**
 * Structure returned by passing APR_LDAP_OPT_API_INFO to
 * apr_ldap_option_get().
 *
 * Use to return information about the underlying LDAP API.
 *
 * @see apr_ldap_option_get
 * @see APR_LDAP_OPT_API_INFO
 */
typedef struct apr_ldap_apiinfo_t {
    /** revision of API supported */
    int api_version;
    /** highest LDAP version supported */
    int protocol_version;
    /** names of API extensions */
    const char **extensions;
    /** name of supplier */
    const char *vendor_name;
    /** supplier-specific version * 100 */
    int vendor_version;
} apr_ldap_apiinfo_t;


/**
 * Structure returned by passing APR_LDAP_OPT_API_FEATURE_INFO to
 * apr_ldap_option_get().
 *
 * Use to return details of extensions supported by the underlying API.
 *
 * @see apr_ldap_option_get
 * @see APR_LDAP_OPT_API_FEATURE_INFO
 */
typedef struct apr_ldap_apifeature_info_t {
    /** LDAP_API_FEATURE_* (less prefix) */
    const char *name;
    /** value of LDAP_API_FEATURE_... */
    int version;
} apr_ldap_apifeature_info_t;

/**
 * LDAP Protocol Versions.
 *
 * @see apr_ldap_option_set
 * @see APR_LDAP_OPT_PROTOCOL_VERSION
 */
typedef enum {
    /** LDAP version 1 */
    APR_LDAP_VERSION1 = 1,
    /** LDAP version 2 */
    APR_LDAP_VERSION2 = 2,
    /** LDAP version 3 */
    APR_LDAP_VERSION3 = 3
} apr_ldap_protocol_version_e;

/**
 * LDAP deref settings
 *  
 * @see apr_ldap_option_set
 * @see APR_LDAP_OPT_DEREF
 */
typedef enum {
    APR_LDAP_DEREF_NEVER = 0,       /**< Aliases should never be dereferenced */
    APR_LDAP_DEREF_SEARCHING = 1,   /**< Aliases should be dereferenced during the search, but not when locating the base object of the search. */
    APR_LDAP_DEREF_FINDING = 2,     /**< Aliases should be dereferenced when locating the base object, but not during the search. */
    APR_LDAP_DEREF_ALWAYS = 3       /**< Aliases should always be dereferenced */
} apr_ldap_deref_e;

/**
 * LDAP options on or off
 *  
 * @see apr_ldap_option_set
 * @see APR_LDAP_OPT_REFERRALS
 */
typedef enum {
    APR_LDAP_OPT_OFF = 0,           /**< Option set off */
    APR_LDAP_OPT_ON = 1             /**< Option set on */
} apr_ldap_switch_e;


/**
 * Set SSL mode to one of APR_LDAP_NONE, APR_LDAP_SSL, APR_LDAP_STARTTLS
 * or APR_LDAP_STOPTLS.
 * @see apr_ldap_option_set
 * @see apr_ldap_option_get
 * @see apr_ldap_tls_e
 */
#define APR_LDAP_OPT_TLS 0x6fff
/**
 * Set zero or more CA certificates, client certificates or private
 * keys globally, or per connection (where supported).
 *
 * @see apr_ldap_option_set
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_OPT_TLS_CERT 0x6ffe
/**
 * Set the LDAP library to not verify the server certificate.  This means
 * all servers are considered trusted.
 * @see apr_ldap_option_set
 * @see apr_ldap_verify_e
 */
#define APR_LDAP_OPT_VERIFY_CERT 0x6ffd
/**
 * Set the LDAP library to indicate if referrals should be chased during
 * LDAP searches.
 * @see apr_ldap_option_get
 * @see apr_ldap_option_set
 * @see apr_ldap_switch_e
 */
#define APR_LDAP_OPT_REFERRALS 0x6ffc
/**
 * Set the LDAP library to indicate a maximum number of referral hops to
 * chase before giving up on the search.
 * @see apr_ldap_option_get
 * @see apr_ldap_option_set
 */
#define APR_LDAP_OPT_REFHOPLIMIT 0x6ffb
/**
 * Get the underlying native LDAP handle.
 * @see apr_ldap_option_get
 */
#define APR_LDAP_OPT_HANDLE 0x6ffa
/**
 * Get/Set the LDAP protocol version.
 * @see apr_ldap_option_get
 * @see apr_ldap_option_set
 * @see apr_ldap_protocol_version_e
 */
#define APR_LDAP_OPT_PROTOCOL_VERSION 0x6ff9
/**
 * Get the LDAP API info.
 * @see apr_ldap_option_get
 * @see apr_ldap_apiinfo_t
 */
#define APR_LDAP_OPT_API_INFO 0x6ff8
/**
 * Get the LDAP API feature info.
 * @see apr_ldap_option_get
 * @see apr_ldap_apifeature_info_t
 */
#define APR_LDAP_OPT_API_FEATURE_INFO 0x6ff7
/**
 * Get the dereference setting.
 * @see apr_ldap_option_get
 * @see apr_ldap_option_set
 * @see apr_ldap_deref_e
 */
#define APR_LDAP_OPT_DEREF 0x6ff6
/**
 * Get the most recent result code.
 * @see apr_ldap_option_get
 */
#define APR_LDAP_OPT_RESULT_CODE 0x6ff5
/**
 * Get or set the underlying socket.
 *
 * Use this to get the underlying socket so as to perform select/poll
 * before attempting to read or write.
 *
 * Note that LDAP libraries like OpenLDAP will successfully return an
 * invalid socket if a previous attempt to connect failed. In this
 * case, you will obtain an error the next time you use the socket.
 *
 * This option can also be used to set the underlying socket, as an
 * alternative to specifying a URI. This is typically done to perform
 * non blocking DNS lookups, or non blocking TLS negotiation, neither
 * of which is supported natively by LDAP APIs.
 *
 * @warning Either APR_LDAP_OPT_DESC or APR_LDAP_OPT_URI must be set
 * before any other options are set, for the LDAP handle to be
 * initialised internally.
 * @see apr_ldap_option_get
 * @see apr_ldap_option_set
 * @see apr_socket_t
 */
#define APR_LDAP_OPT_DESC 0x6ff4
/**
 * Set the URI to connect to.
 *
 * @warning This option (or APR_LDAP_OPT_DESC) must be set before other options,
 * as this initialises the underlying LDAP API.
 * @see apr_ldap_option_set
 */
#define APR_LDAP_OPT_URI 0x5006
/**
 * Get/set the network timeout.
 * @see apr_ldap_option_get
 * @see apr_ldap_option_set
 */
#define APR_LDAP_OPT_NETWORK_TIMEOUT 0x5005
/**
 * Get/set the timeout.
 * @see apr_ldap_option_get
 * @see apr_ldap_option_set
 */
#define APR_LDAP_OPT_TIMEOUT 0x5002

/**
 * CA certificate type unknown
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_CA_TYPE_UNKNOWN    0
/**
 * Binary DER encoded CA certificate
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_CA_TYPE_DER        1
/**
 * PEM encoded CA certificate
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_CA_TYPE_BASE64     2
/**
 * Openldap directory full of base64-encoded cert
 * authorities with hashes in corresponding .0 directory
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_CA_TYPE_CACERTDIR_BASE64 15
/**
 * CA Certificate at the given URI
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_CA_TYPE_URI        18
/**
 * Client certificate type unknown
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_CERT_TYPE_UNKNOWN  5
/**
 * Binary DER encoded client certificate
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_CERT_TYPE_DER      6
/**
 * PEM encoded client certificate
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_CERT_TYPE_BASE64   7
/**
 * PKCS#12 encoded client certificate
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_CERT_TYPE_PFX      13
/**
 * Certificate at the given URI
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_CERT_TYPE_URI      16
/**
 * Private key type unknown
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_KEY_TYPE_UNKNOWN   10
/**
 * Binary DER encoded private key
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_KEY_TYPE_DER       11
/**
 * PEM encoded private key
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_KEY_TYPE_BASE64    12
/**
 * PKCS#12 encoded private key
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_KEY_TYPE_PFX       14
/**
 * Private key at the given URI
 * @see APR_LDAP_OPT_TLS_CERT
 * @see apr_ldap_opt_tls_cert_t
 */
#define APR_LDAP_KEY_TYPE_URI       17



/**
 * Certificate structure.
 *
 * This structure is used to store certificate details. An array of
 * these structures is passed to apr_ldap_option_set() with the option
 * APR_LDAP_OPT_TLS_CERT to set CA and client certificates.
 *
 * @see apr_ldap_option_set
 * @see APR_LDAP_OPT_TLS_CERT
 */
typedef struct apr_ldap_opt_tls_cert_t {
    /** Type of certificate APR_LDAP_*_TYPE_* */
    int type;
    /** Path, file, uri or nickname of the certificate */
    const char *path;
    /** Optional password, can be NULL */
    const char *password;
} apr_ldap_opt_tls_cert_t;


/**
 * APR_LDAP_OPT_TLS
 *
 * This sets the SSL level on the LDAP handle.
 *
 * @see APR_LDAP_OPT_TLS
 * @see apr_ldap_option_set
 */
typedef enum {
    APR_LDAP_TLS_NONE = 0,          /**< No encryption */
    APR_LDAP_TLS_SSL = 1,           /**< SSL encryption (ldaps://) */
    APR_LDAP_TLS_STARTTLS = 2,      /**< TLS encryption (STARTTLS) */
    APR_LDAP_TLS_STOPTLS = 3        /**< end TLS encryption (STOPTLS) */
} apr_ldap_tls_e;


/**
 * LDAP TLS verify options
 *
 * @see APR_LDAP_OPT_VERIFY_CERT
 * @see apr_ldap_option_set
 */
typedef enum {
    /** Disable TLS verification (this is an insecure setting) */
    APR_LDAP_VERIFY_OFF = 0,
    /** Enable TLS verification */
    APR_LDAP_VERIFY_ON = 1
} apr_ldap_verify_e;


/**
 * Union of all possible options to be passed to apr_ldap_option_get()
 * and apr_ldap_option_set().
 *
 * @see apr_ldap_option_set
 * @see apr_ldap_option_get
 */
typedef union apr_ldap_opt_t {
    /**
     * LDAP native handle
     * @see APR_LDAP_OPT_HANDLE
     */
    void *handle;
    /**
     * LDAP native option
     */
    void *opt;
    /**
     * LDAP underlying socket
     *
     * @see APR_LDAP_OPT_DESC
     */
    apr_socket_t *socket;
    /**
     * LDAP uri
     *
     * @see APR_LDAP_OPT_URI
     */
    const char *uri;
    /**
     * LDAP API information
     *
     * @see APR_LDAP_OPT_API_INFO
     */
    apr_ldap_apiinfo_t info;
    /**
     * LDAP API feature information
     *
     * @see APR_LDAP_OPT_API_FEATURE_INFO
     */
    apr_ldap_apifeature_info_t ldfi;
    /**
     * Protocol version
     *
     * @see APR_LDAP_OPT_PROTOCOL_VERSION
     */
    apr_ldap_protocol_version_e pv;
    /**
     * TLS certificates
     *
     * @see APR_LDAP_OPT_TLS_CERT
     */
    apr_array_header_t *certs;
    /**
     * Timeouts
     *
     * @see APR_LDAP_OPT_NETWORK_TIMEOUT
     * @see APR_LDAP_OPT_TIMEOUT     
     */
    apr_interval_time_t timeout;
    /**
     * TLS on/off/starttls
     *
     * @see APR_LDAP_OPT_TLS
     */
    apr_ldap_tls_e tls;
    /**
     * TLS verification
     *
     * @see APR_LDAP_OPT_VERIFY_CERT
     */
    apr_ldap_verify_e verify;
    /**
     * Alias dereference
     *
     * @see APR_LDAP_OPT_DEREF
     */
    apr_ldap_deref_e deref;
    /**
     * Referrals chased
     *
     * @see APR_LDAP_OPT_REFERRALS
     */
    apr_ldap_switch_e refs;
    /**
     * Referral hop limit
     *
     * @see APR_LDAP_OPT_REFHOPLIMIT
     */
    int refhoplimit;
    /**
     * Result code
     *
     * @see APR_LDAP_OPT_RESULT_CODE
     */
    int result;
} apr_ldap_opt_t;


/**
 * APR LDAP get option function
 *
 * This function gets option values from a given LDAP session if
 * one was specified. It maps to the native ldap_get_option() function.
 * @param pool The pool to use where needed
 * @param ldap The LDAP handle
 * @param option The LDAP_OPT_* option to return
 * @param outvalue The value returned (if any)
 * @param result_err On error, error details are written to the
 *        structure.
 * @see APR_LDAP_OPT_API_FEATURE_INFO
 * @see APR_LDAP_OPT_API_INFO
 * @see APR_LDAP_OPT_DEREF
 * @see APR_LDAP_OPT_DESC
 * @see APR_LDAP_OPT_HANDLE
 * @see APR_LDAP_OPT_NETWORK_TIMEOUT
 * @see APR_LDAP_OPT_PROTOCOL_VERSION
 * @see APR_LDAP_OPT_REFERRALS
 * @see APR_LDAP_OPT_REFHOPLIMIT
 * @see APR_LDAP_OPT_RESULT_CODE     
 * @see APR_LDAP_OPT_TIMEOUT
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_option_get(apr_pool_t *pool, apr_ldap_t *ldap,
                                                   int option,
                                                   apr_ldap_opt_t *outvalue,
                                                   apu_err_t *result_err)
                                                   __attribute__((nonnull(1,4,5)));

/**
 * APR LDAP set option function
 *
 * This function sets option values to a given LDAP session if
 * one was specified. It maps to the native ldap_set_option() function.
 *
 * Where an option is not supported by an LDAP toolkit, this function
 * will try and apply legacy functions to achieve the same effect,
 * depending on the platform.
 * @param pool The pool to use where needed
 * @param ldap The LDAP handle
 * @param option The LDAP_OPT_* option to set
 * @param invalue The value to set
 * @param result_err On error, error details are written to the
 *        structure.
 * @see APR_LDAP_OPT_DEREF
 * @see APR_LDAP_OPT_DESC
 * @see APR_LDAP_OPT_NETWORK_TIMEOUT
 * @see APR_LDAP_OPT_PROTOCOL_VERSION
 * @see APR_LDAP_OPT_REFERRALS
 * @see APR_LDAP_OPT_REFHOPLIMIT
 * @see APR_LDAP_OPT_TIMEOUT
 * @see APR_LDAP_OPT_TLS
 * @see APR_LDAP_OPT_TLS_CERT
 * @see APR_LDAP_OPT_URI
 * @see APR_LDAP_OPT_VERIFY_CERT
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_option_set(apr_pool_t *pool, apr_ldap_t *ldap,
                                                   int option,
                                                   const apr_ldap_opt_t *invalue,
                                                   apu_err_t *result_err)
                                                   __attribute__((nonnull(1,5)));

/**
 * LDAP interaction identifiers during LDAP binding
 *
 * @see apr_ldap_bind_interact_t
 * @see apr_ldap_bind
 */
typedef enum {
    APR_LDAP_INTERACT_DN = 0,                     /**< Distinguished name to use for simple bind */
    APR_LDAP_INTERACT_GETREALM = 0x4008,          /**< SASL realm for the authentication attempt */
    APR_LDAP_INTERACT_AUTHNAME = 0x4002,          /**< SASL username to authenticate */
    APR_LDAP_INTERACT_USER = 0x4001,              /**< SASL username to use for proxy authorization */
    APR_LDAP_INTERACT_PASS = 0x4004,              /**< SASL password for the provided username / Simple password for a simple bind */
    APR_LDAP_INTERACT_NOECHOPROMPT = 0x4006,      /**< SASL generic prompt for input with input echoing disabled */
    APR_LDAP_INTERACT_ECHOPROMPT = 0x4005,        /**< SASL generic prompt for input with input echoing enabled */
} apr_ldap_bind_interact_e;


/**
 * During apr_ldap_bind(), a callback is passed this structure
 * requesting authentication and authorisation details. The callback
 * is expected to fill the buffer with the information requested.
 *
 * This is used to obtain the information needed for SASL binds.
 *
 * @see apr_ldap_bind_interact_e
 * @see apr_ldap_bind
 */
typedef struct apr_ldap_bind_interact_t {
    /** An enum indicating what information is requested. */
    apr_ldap_bind_interact_e id;
    /** Presented to user (e.g. OTP challenge) */
    const char *challenge;
    /** Presented to user (e.g. "Username: ") */
    const char *prompt;
    /** Default result string */
    const char *defresult;
    /** Buffer to be filled in by the callback with the information requested */
    apr_buffer_t result;
} apr_ldap_bind_interact_t;

/**
 * Bind SASL interact callback.
 *
 * Depending on the type of SASL mechanism chosen, this callback is called
 * to request details needed for each bind.
 *
 * @see apr_ldap_bind_interact_t
 * @see apr_ldap_bind
 */ 
typedef apr_status_t (apr_ldap_bind_interact_cb)(
        apr_ldap_t *ld, unsigned int flags, apr_ldap_bind_interact_t *interact, void *ctx);





#if 0

typedef struct apr_ldap_rebind_t {
    /** presented to user (e.g. OTP challenge) */
    const char *challenge;
    /** presented to user (e.g. "Username: ") */
    const char *prompt;
} apr_ldap_rebind_t;

typedef apr_status_t (apr_ldap_rebind_proc)(
        apr_ldap_t *ld, apr_ldap_rebind_t *rebind, void *ctx);

#endif



/**
 * LDAP Control structure
 *
 * @see apr_ldap_bind_cb
 * @see apr_ldap_compare_cb
 * @see apr_ldap_search_result_cb
 * @see apr_ldap_compare
 * @see apr_ldap_search
 */
typedef struct apr_ldap_control_t apr_ldap_control_t;




/**
 * APR LDAP connect function. 
 *  
 * This function makes an attempt to connect to the server initialised
 * by apr_ldap_initialise().
 *
 * While other functions will connect if not connected, use this
 * function to explicitly handle errors in the connect case.
 *
 * This function will synchronously perform DNS lookups and TLS negotiation
 * and will block if needed.
 *
 * If you need asynchronous handling, perform the DNS and TLS handling
 * yourself, and then pass the socket with APR_LDAP_OPT_DESC.
 *
 * @return APR_SUCCESS means that the connection connected successfully.
 * Other error codes indicate that the connect was not successful.
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_connect(apr_pool_t *pool,
                                                apr_ldap_t *ldap,
                                                apr_interval_time_t timeout,
                                                apu_err_t *result_err)
                                                __attribute__((nonnull(1,2,4)));

/**
 * Callback to prepare an LDAP request.
 *
 * This callback is scheduled to be fired when the LDAP socket is next
 * writable, from within apr_ldap_process().
 *
 * When complete, return APR_SUCCESS to indicate you want to continue, or
 * a different code if you want the event loop to give up. This code will
 * be returned from apr_ldap_process().
 * @see apr_ldap_prepare
 * @see apr_ldap_process
 */
typedef apr_status_t (*apr_ldap_prepare_cb)(apr_ldap_t *ldap, apr_status_t status,
                                            void *ctx, apu_err_t *err);
 

/**
 * APR LDAP prepare function
 *
 * This function schedules a generic callback, fired the next time the LDAP
 * socket is writable.
 *
 * This callback can be used to prepare the initial LDAP request, or to
 * prepare additional requests as needed without blocking.
 *
 * @param pool The pool that keeps track of the lifetime of the callback.
 * If this pool is cleaned up, the callback will be will be gracefully
 * removed without affecting other LDAP requests in progress. This pool need
 * not have any relationship with the LDAP connection pool.
 * @param ldap The ldap handle
 * @param prepare_cb The prepare callback function. When apr_ldap_process() is
 * next called this callback will be triggered in the expectation of the next
 * LDAP request.
 * @param prepare_ctx Context passed to the prepare callback.
 * @param err Error structure for reporting detailed results.
 *
 * @return APR_SUCCESS means the callback was successfully prepared. Other error
 * codes indicate that the attept to send the cancellation was not successful.
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_prepare(apr_pool_t *pool,
                                                apr_ldap_t *ldap,
                                                apr_ldap_prepare_cb prepare_cb,
                                                void *prepare_ctx)
                                                __attribute__((nonnull(1,2,3)));


/**
 * APR process function.
 *
 * This function performs outstanding processing of any LDAP conversations
 * currently in progress.
 *
 * When a request tells you that further processing is needed, schedule this
 * call the next time the socket is writable.
 *
 * Most callbacks are fired from within apr_ldap_process() so that we are
 * ready to write the next LDAP query should that be needed.
 *
 * @param pool The pool to use
 * @param ldap The LDAP handle
 * @param timeout The timeout to use for writes.
 * @param err Error structure for reporting detailed results.
 *
 * @return APR_WANT_WRITE means that at least one further process is outstanding
 * and a further write callback should be scheduled. APR_WANTS_READ indicates
 * the a request has been sent and we're waiting for the response. APR_SUCCESS
 * means that no further processing is needed. Other error codes indicate that
 * the processing of outstanding conversations was not successful.
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_process(apr_pool_t *pool,
                                                apr_ldap_t *ldap,
                                                apr_interval_time_t timeout,
                                                apu_err_t *err)
                                                __attribute__((nonnull(1,2,4)));


/**
 * APR result function.
 *
 * This function returns the result of a previous request, ready for further
 * processing.
 *
 * @param pool The pool to use
 * @param ldap The LDAP handle
 * @param timeout The timeout to use for writes.
 * @param err Error structure for reporting detailed results.
 *
 * @return APR_WANT_WRITE means that at least one further process is outstanding
 * and a further write callback should be scheduled. APR_WANTS_READ indicates
 * more responses are expected and we're waiting for the response. APR_SUCCESS
 * means that no further processing is needed. Other error codes indicate that 
 * the processing of outstanding conversations was not successful.
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_result(apr_pool_t *pool,
                                               apr_ldap_t *ldap,
                                               apr_interval_time_t timeout,
                                               apu_err_t *err)
                                               __attribute__((nonnull(1,2,4)));


/**
 * APR LDAP poll function.
 *
 * For applications that need simple set of queries, this function provides
 * an event loop that can handle a series of LDAP requests.
 *
 * This function calls apr_ldap_process() and apr_ldap_result() as needed.
 *
 * @param pool The pool to use
 * @param ldap The LDAP handle
 * @param timeout The timeout to use for reads and writes.
 * @param err Error structure for reporting detailed results.
 *
 * @return APR_SUCCESS means that no further processing is needed. Other error
 * codes indicate that processing was not successful.
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_poll(apr_pool_t *pool,
                                             apr_ldap_t *ldap,
                                             apr_pollcb_t *poll,
                                             apr_interval_time_t timeout,
                                             apu_err_t *err)
                                             __attribute__((nonnull(1,2,3,5)));


/** 
 * Callback to receive the results of a bind operation.
 *
 * When a bind is successful, this function is called with a status of
 * APR_SUCCESS.
 *
 * Bind success is returned from within apr_ldap_process(), and therefore
 * it can be safely assumed that the underlying socket is writable ready
 * for exactly one further LDAP operation like apr_ldap_search() or
 * apr_ldap_compare().
 *
 * If the bind fails, status will carry the error code, and err will return
 * the human readable details.
 *
 * If the underlying LDAP connection has failed, status will return details
 * of the error, allowing an opportunity to clean up.
 *
 * When complete, return APR_SUCCESS to indicate you want to continue, or
 * a different code if you want the event loop to give up. This code will
 * be returned from apr_ldap_process().
 *
 * If this callback was called during a pool cleanup, the return value is
 * ignored.
 * @see apr_ldap_bind
 * @see apr_ldap_process
 * @see apr_ldap_result
 */ 
typedef apr_status_t (*apr_ldap_bind_cb)(apr_ldap_t *ldap, apr_status_t status,
                                         const char *matcheddn,
                                         apr_ldap_control_t **serverctrls,
                                         void *ctx, apu_err_t *err);


#if 0
/** 
 * Function called to report cancel results.
 */ 
typedef void (*apr_ldap_cancel_cb)(apr_ldap_t *ldap, apr_ldap_message_t *msg, void *ctx);

/**
 * APR LDAP cancel function
 *
 * This function cancels a previously sent LDAP operation, identified by
 * the callback function and callback context.
 *
 * Cancellations are attempted asynchronously. The result of the cancellation
 * will be retrieved and handled by the apr_ldap_result() function, and the
 * outcome is passed to the callback provided.
 *
 * @return APR_INCOMPLETE means that the cancellation was sent, and the message
 * in reply needs to be fetched using apr_ldap_result(). Other error
 * codes indicate that the attept to send the cancellation was not successful.
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_cancel(apr_pool_t *pool,
                                               apr_ldap_t *ldap,
                                               apr_ldap_control_t **serverctrls,
                                               apr_ldap_control_t **clientctrls,
                                               apr_interval_time_t timeout,
                                               apr_ldap_cancel_cb cancel_cb, void *cancel_ctx,
                                               apu_err_t *err)
                                               __attribute__((nonnull(1,2,6,8)));
#endif

/**
 * APR LDAP bind function
 *
 * This function initiates a bind on a previously initialised LDAP connection
 * to the directory.
 *
 * Pass the required SASL mechanism in mech, or set to NULL for a simple
 * bind.
 *
 * Unlike the native LDAP APIs, this function muct be called just once.
 * The job of binding is done inside apr_ldap_process() and apr_ldap_result().
 *
 * Binds are attempted asynchronously. For non blocking behaviour, this function
 * must be called after the underlying socket has indicated that it is ready to
 * write.
 *
 * In the absence of an error, apr_ldap_bind will return APR_WANT_READ to
 * indicate that the next message in the conversation be retrieved using
 * apr_ldap_result().
 *
 * The outcome of the bind will be retrieved and handled by the
 * apr_ldap_process() function, and the outcome is passed to the
 * apr_ldap_bind_cb provided.
 *
 * @param pool The pool that keeps track of the lifetime of the bind conversation.
 * If this pool is cleaned up, the bind conversation will be gracefully
 * abandoned without affecting other LDAP requests in progress. This pool need
 * not have any relationship with the LDAP connection pool.
 * @param ldap The ldap handle
 * @param mech The SASL mechanism. Pass NULL for simple bind.
 * @param interact_cb The SASL interactive callback function. This function is
 * is called to request credentials for the bind, depending on the mechanism.
 * @param interact_ctx Context passed to the interactive callback.
 * @param timeout The timeout to use for writes.
 * @param bind_cb The bind result callback function. When the bind process has
 * completed the success or failure of the bind is returned here. The callback
 * is triggered from inside apr_ldap_process() so that it is safe to write the
 * next LDAP request.
 * @param bind_ctx Context passed to the bind callback.
 * @param err Error structure for reporting detailed results.
 * @return APR_WANT_READ means that processing has occurred, and
 * the message in reply needs to be fetched using apr_ldap_result().
 * APR_WANT_WRITE means that processing has occurred, and the
 * conversation needs to be continued with a call to apr_ldap_process().
 * APR_SUCCESS means that the processing is complete, and the bind
 * has been successful. Other error codes indicate that the bind
 * was not successful.
 * @see apr_ldap_bind_interact_cb
 * @see apr_ldap_bind_cb
 * @see apr_ldap_process
 * @see apr_ldap_result
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_bind(apr_pool_t *pool, apr_ldap_t *ldap,
                                             const char *mech,
                                             apr_ldap_bind_interact_cb *interact_cb,
                                             void *interact_ctx,
                                             apr_interval_time_t timeout,
                                             apr_ldap_bind_cb bind_cb, void *bind_ctx,
                                             apu_err_t *err)
                                             __attribute__((nonnull(1,2,4,9)));


/**
 * Callback to receive the results of a compare operation.
 *
 * When a compare is successful, this function is called with a status of
 * APR_COMPARE_TRUE or APR_COMPARE_FALSE.
 *
 * If the compare fails, status will carry the error code, and err will return
 * the human readable details.
 *
 * If the underlying LDAP connection has failed, status will return details
 * of the error, allowing an opportunity to clean up.
 *
 * When complete, return APR_SUCCESS to indicate you want to continue, or
 * a different code if you want the event loop to give up. This code will
 * be returned from apr_ldap_result().
 *
 * If this callback was called during a pool cleanup, the return value is
 * ignored.
 * @see apr_ldap_compare
 * @see apr_ldap_result
 */
typedef apr_status_t (*apr_ldap_compare_cb)(apr_ldap_t *ldap, apr_status_t status,
                                            const char *matcheddn,
                                            apr_ldap_control_t **serverctrls,
                                            void *ctx, apu_err_t *err);



/**
 * APR LDAP compare function
 *
 * This function compares a string or binary value of an attribute
 * within an entry described by the given distinguished name against
 * a previously initialised LDAP connection to the directory.
 *
 * Compares are attempted asynchronously. For non blocking behaviour, this function
 * must be called after the underlying socket has indicated that it is ready to
 * write.
 *
 * In the absence of an error, apr_ldap_compare will return APR_WANT_READ to
 * indicate that the next message in the conversation be retrieved using
 * apr_ldap_result().
 *
 * The outcome of the compare will be retrieved and handled by the
 * apr_ldap_process() function, and the outcome is passed to the
 * apr_ldap_compare_cb provided.
 *
 * @param pool The pool that keeps track of the lifetime of the compare conversation.
 * If this pool is cleaned up, the compare conversation will be gracefully 
 * abandoned without affecting other LDAP requests in progress. This pool need
 * not have any relationship with the LDAP connection pool.
 * @param ldap The ldap handle
 * @param dn The distinguished named of the object to compare.
 * @param attr The attribute of the object to compare.
 * @param val The value to be compared to the attribute. The value can be zero
 * terminated text, or binary.
 * @param serverctrls NULL terminated array of server controls.
 * @param clientctrls NULL terminated array of client controls.
 * @param timeout The timeout to use for writes. 
 * @param compare_cb The compare result callback function. When the compare process has
 * completed the success or failure of the compare is returned here. The callback
 * is triggered from inside apr_ldap_process() so that it is safe to write the
 * next LDAP request.
 * @param ctx Context passed to the compare callback.
 * @param err Error structure for reporting detailed results.
 *
 * @return APR_WANT_READ means that processing has occurred, and
 * the message in reply needs to be fetched using apr_ldap_result().
 * APR_SUCCESS means that the processing is complete, and the bind
 * has been successful. Other error codes indicate that the bind
 * was not successful.
 * @see apr_ldap_compare_cb
 * @see apr_ldap_process
 * @see apr_ldap_result
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_compare(apr_pool_t *pool,
                                                apr_ldap_t *ldap,
                                                const char *dn,
                                                const char *attr,
                                                const apr_buffer_t *val,
                                                apr_ldap_control_t **serverctrls,
                                                apr_ldap_control_t **clientctrls,
                                                apr_interval_time_t timeout,
                                                apr_ldap_compare_cb compare_cb, void *ctx,
                                                apu_err_t *err)
                                               __attribute__((nonnull(1,2,3,4,5,11)));


/**
 * APR search scopes
 *
 * @see apr_ldap_search
 */
typedef enum {
    /** base object search */
    APR_LDAP_SCOPE_BASE = 0x0000,
    /** one-level search */
    APR_LDAP_SCOPE_ONELEVEL = 0x0001,
    /** subtree search */
    APR_LDAP_SCOPE_SUBTREE = 0x0002,
    /** subordinate search */
    APR_LDAP_SCOPE_SUBORDINATE = 0x0003
} apr_ldap_search_scope_e;


/**
 * Callback to receive the results of a search operation.
 *
 * This callback is fired once for every search.
 *
 * When a search is complete, this function is called with a status of
 * APR_SUCCESS or APR_NO_RESULTS_RETURNED.
 *
 * If the search fails, status will carry the error code, and err will return
 * the human readable details.
 *
 * If the underlying LDAP connection has failed, status will return details
 * of the error, allowing an opportunity to clean up.
 *
 * When complete, return APR_SUCCESS to indicate you want to continue, or
 * a different code if you want the event loop to give up. This code will
 * be returned from apr_ldap_result().
 *
 * If this callback was called during a pool cleanup, the return value is
 * ignored.
 * @see apr_ldap_search
 * @see apr_ldap_result
 */
typedef apr_status_t (*apr_ldap_search_result_cb)(apr_ldap_t *ldap, apr_status_t status,
                                                  apr_size_t count, const char *matcheddn,
                                                  apr_ldap_control_t **serverctrls,
                                                  void *ctx, apu_err_t *err);

/**
 * Callback to receive the entries of a search operation.
 *
 * This callback is fired once for every attribute and value combination,
 * and then once for each entry to indicate the entry is complete.
 *
 * When complete, return APR_SUCCESS to indicate you want to continue, or
 * a different code if you want the event loop to give up. This code will 
 * be returned from apr_ldap_result().
 *
 * @see apr_ldap_search
 * @see apr_ldap_result
 */
typedef apr_status_t (*apr_ldap_search_entry_cb)(apr_ldap_t *ldap, const char *dn,
                                                 int eidx, int nattrs, int aidx,
                                                 const char *attr, int nvals,
                                                 int vidx, apr_buffer_t *val, int binary,
                                                 void *ctx, apu_err_t *err);


/**
 * APR LDAP search function
 *      
 * This function searches a previously initialised LDAP connection to the directory.
 *  
 * Searches are attempted asynchronously. For non blocking behaviour, this function
 * must be called after the underlying socket has indicated that it is ready to
 * write.
 *
 * In the absence of an error, apr_ldap_search will return APR_WANT_READ to
 * indicate that the next message in the conversation be retrieved using
 * apr_ldap_result().
 *
 * The outcome of the search will be retrieved and handled by the
 * apr_ldap_result() function as each result arrives.
 *
 * If one or more results are returned, the apr_ldap_search_entry_cb callback
 * is called once for each attribute and value combination.
 *
 * At the end of each entry, apr_ldap_search_entry_cb will be called with no
 * attribute or value, giving code an opportunity to perform any processing only
 * possible after all of the entries have been retrieved.
 *
 * Once all entries have been processed, apr_ldap_search_result_cb is called to
 * indicate the final result of the search.
 *
 * If no entries are returned, only apr_ldap_search_result_cb will be called.
 *
 * @param pool The pool that keeps track of the lifetime of the search conversation.
 * If this pool is cleaned up, the search conversation will be gracefully
 * abandoned without affecting other LDAP requests in progress. This pool need
 * not have any relationship with the LDAP connection pool.
 * @param ldap The ldap handle
 * @param dn The base distinguished named of the search.
 * @param scope The scope of the search.
 * @param filter The search filter string.
 * @param attrs NULL terminated array of attributes to return.
 * @param attrsonly If on, attributes will be returned without values.
 * @param serverctrls NULL terminated array of server controls.
 * @param clientctrls NULL terminated array of client controls.
 * @param timeout The timeout to use for writes.
 * @param sizelimit The maximum number of entries to return in the search.
 * @param search_result_cb The search result callback function. When the search
 * process has completed the success or failure of the search is returned here.
 * The callback is triggered from inside apr_ldap_process() so that it is safe to
 * write the next LDAP request.
 * @param search_entry_cb The search entry callback function. For each value of
 * each attribute of each entry, this callback is called with each value. This
 * callback is then fired off one more time at the end of each entry, giving the
 * chance to handle that entry. The callback is triggered from inside
 * apr_ldap_result().
 * @param ctx Context passed to the search result and search entry callbacks.
 * @param err Error structure for reporting detailed results.
 *
 * @return APR_WANT_READ means that processing has occurred, and
 * the message in reply needs to be fetched using apr_ldap_result().
 * Other error codes indicate that the search attempt was not successful.
 * @see apr_ldap_search_entry_cb
 * @see apr_ldap_search_result_cb
 * @see apr_ldap_result
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_search(apr_pool_t *pool,
                                               apr_ldap_t *ldap,
                                               const char *dn,
                                               apr_ldap_search_scope_e scope,
                                               const char *filter,
                                               const char **attrs,
                                               apr_ldap_switch_e attrsonly,
                                               apr_ldap_control_t **serverctrls,
                                               apr_ldap_control_t **clientctrls,
                                               apr_interval_time_t timeout,
                                               apr_ssize_t sizelimit,
                                               apr_ldap_search_result_cb search_result_cb,
                                               apr_ldap_search_entry_cb search_entry_cb,
                                               void *ctx,
                                               apu_err_t *err)
                                               __attribute__((nonnull(1,2,3,15)));

/**
 * APR LDAP unbind function
 *
 * This function unbinds from the LDAP server, and frees the connection handle.
 *
 * Calling this function is optional, the same effect can be achieved by cleaning up
 * the pool passed to apr_ldap_initialise().
 *
 * @see apr_ldap_initialise
 */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_unbind(apr_ldap_t *ldap,
                                               apr_ldap_control_t **serverctrls,
                                               apr_ldap_control_t **clientctrls,
                                               apu_err_t *err)
                                               __attribute__((nonnull(1,4)));


#endif /* APU_HAS_LDAP */
/** @} */
#endif /* APU_LDAP_H */

