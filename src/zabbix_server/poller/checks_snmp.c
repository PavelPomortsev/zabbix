/* 
** ZABBIX
** Copyright (C) 2000-2005 SIA Zabbix
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**/

#include "checks_snmp.h"

#ifdef HAVE_SNMP

/* Function: snmp_get_index
 *                                                                            *
 * Purpose: find index of OID with given value                                *
 *                                                                            *
 * Parameters: DB_ITEM *item - configuration of zabbix item                   *
 *             char *OID     - OID of table with values of interest           *
 *             char *value   - value to look for                              *
 *             int  *idx     - result to be placed here                       *
 *                                                                            *
 * Return value:  NOTSUPPORTED - OID does not exist, any other critical error *
 *                NETWORK_ERROR - recoverable network error                   *
 *                SUCCEED - success, variable 'idx' contains index having     * 
 *                          value 'value'                                     */
static int snmp_get_index (DB_ITEM * item, char *OID, char *value, int *idx)
{
	const char *__function_name = "snmp_get_index";
	struct snmp_session session, *ss;
	struct snmp_pdu *pdu;
	struct snmp_pdu *response;

	oid anOID[MAX_OID_LEN];
	oid rootOID[MAX_OID_LEN];
	size_t anOID_len = MAX_OID_LEN;
	size_t rootOID_len = MAX_OID_LEN;

	char temp[MAX_STRING_LEN];
	char strval[MAX_STRING_LEN];
	char error[MAX_STRING_LEN];
	struct variable_list *vars;

	int len;
	int status;
	int running;

	int ret = NOTSUPPORTED;

	zabbix_log (LOG_LEVEL_DEBUG, "In %s(oid:%s)",
		__function_name,
		OID);

	*idx = 0;

	assert ((item->type == ITEM_TYPE_SNMPv1) ||
			(item->type == ITEM_TYPE_SNMPv2c) ||
			(item->type == ITEM_TYPE_SNMPv3));

	snmp_sess_init (&session);

	switch (item->type)
	{
	case ITEM_TYPE_SNMPv1:
		session.version = SNMP_VERSION_1;
		break;

	case ITEM_TYPE_SNMPv2c:
		session.version = SNMP_VERSION_2c;
		break;

	case ITEM_TYPE_SNMPv3:
		session.version = SNMP_VERSION_3;
		break;

	default:
		zbx_snprintf (error, sizeof (error),
						"Error in %s. Wrong item type [%d]. Must be SNMP.",
						__function_name,
						item->type);
		zabbix_log (LOG_LEVEL_ERR, "%s", error);
		return NOTSUPPORTED;
	}


	if (item->useip == 1)
	{
		zbx_snprintf (temp, sizeof (temp), "%s:%d", item->host_ip,
					  item->snmp_port);
		session.peername = temp;
		session.remote_port = item->snmp_port;
	}
	else
	{
		zbx_snprintf (temp, sizeof (temp), "%s:%d",
					  item->host_dns, item->snmp_port);
		session.peername = temp;
		session.remote_port = item->snmp_port;
	}

	if ((session.version == SNMP_VERSION_1)
		|| (item->type == ITEM_TYPE_SNMPv2c))
	{
		session.community = (u_char *) item->snmp_community;
		session.community_len = strlen ((void *) session.community);
		zabbix_log (LOG_LEVEL_DEBUG, "SNMP [%s@%s:%d]",
					session.community, session.peername, session.remote_port);
	}
	else if (session.version == SNMP_VERSION_3)
	{
		/* set the SNMPv3 user name */
		session.securityName = item->snmpv3_securityname;
		session.securityNameLen = strlen (session.securityName);

		/* set the security level to authenticated, but not encrypted */

		if (item->snmpv3_securitylevel ==
			ITEM_SNMPV3_SECURITYLEVEL_NOAUTHNOPRIV)
		{
			session.securityLevel = SNMP_SEC_LEVEL_NOAUTH;
		}
		else if (item->snmpv3_securitylevel ==
				 ITEM_SNMPV3_SECURITYLEVEL_AUTHNOPRIV)
		{
			session.securityLevel = SNMP_SEC_LEVEL_AUTHNOPRIV;

			/* set the authentication method to MD5 */
			session.securityAuthProto = usmHMACMD5AuthProtocol;
			session.securityAuthProtoLen = USM_AUTH_PROTO_MD5_LEN;
			session.securityAuthKeyLen = USM_AUTH_KU_LEN;

			if (generate_Ku (session.securityAuthProto,
							 session.securityAuthProtoLen,
							 (u_char *) item->snmpv3_authpassphrase,
							 strlen (item->snmpv3_authpassphrase),
							 session.securityAuthKey,
							 &session.securityAuthKeyLen) != SNMPERR_SUCCESS)
			{
				zbx_snprintf (error, sizeof (error),
							  "Error generating Ku from authentication pass phrase.");

				zabbix_log (LOG_LEVEL_ERR, "%s", error);
				return NOTSUPPORTED;
			}
		}
		else if (item->snmpv3_securitylevel ==
				 ITEM_SNMPV3_SECURITYLEVEL_AUTHPRIV)
		{
			session.securityLevel = SNMP_SEC_LEVEL_AUTHPRIV;

			/* set the authentication method to MD5 */
			session.securityAuthProto = usmHMACMD5AuthProtocol;
			session.securityAuthProtoLen = USM_AUTH_PROTO_MD5_LEN;
			session.securityAuthKeyLen = USM_AUTH_KU_LEN;

			if (generate_Ku (session.securityAuthProto,
							 session.securityAuthProtoLen,
							 (u_char *) item->snmpv3_authpassphrase,
							 strlen (item->snmpv3_authpassphrase),
							 session.securityAuthKey,
							 &session.securityAuthKeyLen) != SNMPERR_SUCCESS)
			{
				zbx_snprintf (error, sizeof (error),
							  "Error generating Ku from authentication pass phrase.");
				zabbix_log (LOG_LEVEL_ERR, "%s", error);
				return NOTSUPPORTED;
			}

			/* set the private method to DES */
			session.securityPrivProto = usmDESPrivProtocol;
			session.securityPrivProtoLen = USM_PRIV_PROTO_DES_LEN;
			session.securityPrivKeyLen = USM_PRIV_KU_LEN;

			if (generate_Ku (session.securityAuthProto,
							 session.securityAuthProtoLen,
							 (u_char *) item->snmpv3_privpassphrase,
							 strlen (item->snmpv3_privpassphrase),
							 session.securityPrivKey,
							 &session.securityPrivKeyLen) != SNMPERR_SUCCESS)
			{
				zbx_snprintf (error, sizeof (error),
							  "Error generating Ku from priv pass phrase.");
				zabbix_log (LOG_LEVEL_ERR, "%s", error);
				return NOTSUPPORTED;
			}
		}
		zabbix_log (LOG_LEVEL_DEBUG, "SNMPv3 [%s@%s:%d]",
					session.securityName,
					session.peername, session.remote_port);
	}
	else
	{
		zbx_snprintf (error, sizeof (error),
					  "Error in %s. Unsupported session version [%d]",
					  __function_name, (int) session.version);
		zabbix_log (LOG_LEVEL_ERR, "%s", error);
		return NOTSUPPORTED;
	}

	zabbix_log (LOG_LEVEL_DEBUG, "OID [%s]", OID);

	if (NULL != CONFIG_SOURCE_IP)
		session.localname = CONFIG_SOURCE_IP;

	SOCK_STARTUP;
	ss = snmp_open (&session);

	if (ss == NULL)
	{
		SOCK_CLEANUP;

		zbx_snprintf (error, sizeof (error), "Error doing snmp_open()");
		zabbix_log (LOG_LEVEL_ERR, "%s", error);
		return NOTSUPPORTED;
	}

	/* create OID from string */
	snmp_parse_oid (OID, rootOID, &rootOID_len);
	/* copy rootOID to anOID */
	memcpy (anOID, rootOID, rootOID_len * sizeof (oid));
	anOID_len = rootOID_len;

	running = 1;
	while (running)
	{
		zabbix_log (LOG_LEVEL_DEBUG, "%s: snmp_pdu_create()",
					__function_name);
		/* prepare PDU */
		pdu = snmp_pdu_create (SNMP_MSG_GETNEXT);	/* create empty PDU */
		snmp_add_null_var (pdu, anOID, anOID_len);	/* add OID as variable to PDU */
		/* communicate with agent */
		status = snmp_synch_response (ss, pdu, &response);

		/* process response */
		if (status == STAT_SUCCESS)
		{
			if (response->errstat == SNMP_ERR_NOERROR)
			{
				for (vars = response->variables; vars && running;
					 vars = vars->next_variable)
				{
					memcpy (strval, vars->val.string, vars->val_len);
					strval[vars->val_len] = 0;	/* terminate */

					len =
						snprint_objid (temp, sizeof (temp), vars->name,
									   vars->name_length);
					zabbix_log (LOG_LEVEL_DEBUG,
								"VAR: %s = %s (type=%d)(length = %d)", temp,
								strval, vars->type, vars->val_len);

					/* verify if we are in the same subtree */
					if ((vars->name_length < rootOID_len) ||
						(memcmp
						 (rootOID, vars->name,
						  rootOID_len * sizeof (oid)) != 0))
					{
						/* not part of this subtree */
						running = 0;
						zabbix_log (LOG_LEVEL_ERR, "NOT FOUND: %s[%s]", OID,
									value);
						ret = NOTSUPPORTED;
					}
					else
					{
						/* verify if OIDs are increasing */
						if ((vars->type != SNMP_ENDOFMIBVIEW) &&
							(vars->type != SNMP_NOSUCHOBJECT) &&
							(vars->type != SNMP_NOSUCHINSTANCE))
						{
							/* not an exception value */
							if (snmp_oid_compare
								(anOID, anOID_len, vars->name,
								 vars->name_length) >= 0)
							{
								zabbix_log (LOG_LEVEL_ERR,
											"Error: OID not increasing.");
								ret = NOTSUPPORTED;
								running = 0;
							}

							/*__compare with key value__ */
							if (strcmp (value, strval) == 0)
							{
								*idx = vars->name[vars->name_length - 1];
								zabbix_log (LOG_LEVEL_DEBUG,
											"FOUND: Index is %d", *idx);
								ret = SUCCEED;
								running = 0;
							}

							/* go to next variable */
							memmove ((char *) anOID, (char *) vars->name,
									 vars->name_length * sizeof (oid));
							anOID_len = vars->name_length;
						}
						else
						{
							/* an exception value, so stop */
							zabbix_log (LOG_LEVEL_DEBUG,
										"%s: Exception value found",
										__function_name);
							running = 0;
							ret = NOTSUPPORTED;
						}
					}			/*same subtree */
				}				/*for */
			}
			else
			{
				zbx_snprintf (error, sizeof (error), "SNMP error [%s]",
							  snmp_errstring (response->errstat));
				zabbix_log (LOG_LEVEL_ERR, "%s", error);
				running = 0;
				ret = NOTSUPPORTED;
			}
		}
		else if (status == STAT_TIMEOUT)
		{
			zbx_snprintf (error, sizeof (error),
						  "Timeout while connecting to [%s]",
						  session.peername);
			zabbix_log (LOG_LEVEL_ERR, "%s", error);
			running = 0;
			ret = NETWORK_ERROR;
		}
		else
		{
			zbx_snprintf (error, sizeof (error), "SNMP error [%d]", status);
			zabbix_log (LOG_LEVEL_ERR, "%s", error);
			running = 0;
			ret = NOTSUPPORTED;
		}

		if (response)
		{
			zabbix_log (LOG_LEVEL_DEBUG, "%s: snmp_free_pdu()",
						__function_name);
			snmp_free_pdu (response);
		}
	}							/* while(running) */

	snmp_close (ss);

	SOCK_CLEANUP;

	zabbix_log (LOG_LEVEL_DEBUG, "%s: end", __function_name);
	return ret;
}


int	get_snmp(DB_ITEM *item, char *snmp_oid, AGENT_RESULT *value)
{

	#define NEW_APPROACH

	struct snmp_session session, *ss;
	struct snmp_pdu *pdu;
	struct snmp_pdu *response;

#ifdef NEW_APPROACH
	char temp[MAX_STRING_LEN];
#endif

	oid anOID[MAX_OID_LEN];
	size_t anOID_len = MAX_OID_LEN;

	struct variable_list *vars;
	int status;

	char 	*p, *c;

	unsigned char *ip;

	char error[MAX_STRING_LEN];

	int ret=SUCCEED;

	zabbix_log( LOG_LEVEL_DEBUG, "In get_snmp(oid:%s)",
		snmp_oid);

	init_result(value);

/*	assert((item->type == ITEM_TYPE_SNMPv1)||(item->type == ITEM_TYPE_SNMPv2c)); */
	assert((item->type == ITEM_TYPE_SNMPv1)||(item->type == ITEM_TYPE_SNMPv2c)||(item->type == ITEM_TYPE_SNMPv3));

	snmp_sess_init( &session );
/*	session.version = version;*/
	if(item->type == ITEM_TYPE_SNMPv1)
	{
		session.version = SNMP_VERSION_1;
	}
	else if(item->type == ITEM_TYPE_SNMPv2c)
	{
		session.version = SNMP_VERSION_2c;
	}
	else if(item->type == ITEM_TYPE_SNMPv3)
	{
		session.version = SNMP_VERSION_3;
	}
	else
	{
		zbx_snprintf(error,sizeof(error),"Error in get_value_SNMP. Wrong item type [%d]. Must be SNMP.",
			item->type);
		zabbix_log( LOG_LEVEL_ERR, "%s",
			error);
		SET_MSG_RESULT(value, strdup(error));

		return NOTSUPPORTED;
	}


	if(item->useip == 1)
	{
	#ifdef NEW_APPROACH
		zbx_snprintf(temp,sizeof(temp),"%s:%d",
			item->host_ip,
			item->snmp_port);
		session.peername = temp;
		session.remote_port = item->snmp_port;
	#else
		session.peername = item->host_ip;
		session.remote_port = item->snmp_port;
	#endif
	}
	else
	{
	#ifdef NEW_APPROACH
		zbx_snprintf(temp, sizeof(temp), "%s:%d",
			item->host_dns,
			item->snmp_port);
		session.peername = temp;
		session.remote_port = item->snmp_port;
	#else
		session.peername = item->host_dns;
		session.remote_port = item->snmp_port;
	#endif
	}

	if( (session.version == SNMP_VERSION_1) || (item->type == ITEM_TYPE_SNMPv2c))
	{
		session.community = (u_char *)item->snmp_community;
		session.community_len = strlen((void *)session.community);
		zabbix_log( LOG_LEVEL_DEBUG, "SNMP [%s@%s:%d]",
			session.community,
			session.peername,
			session.remote_port);
	}
	else if(session.version == SNMP_VERSION_3)
	{
		/* set the SNMPv3 user name */
		session.securityName = item->snmpv3_securityname;
		session.securityNameLen = strlen(session.securityName);

		/* set the security level to authenticated, but not encrypted */

		if(item->snmpv3_securitylevel == ITEM_SNMPV3_SECURITYLEVEL_NOAUTHNOPRIV)
		{
			session.securityLevel = SNMP_SEC_LEVEL_NOAUTH;
		}
		else if(item->snmpv3_securitylevel == ITEM_SNMPV3_SECURITYLEVEL_AUTHNOPRIV)
		{
			session.securityLevel = SNMP_SEC_LEVEL_AUTHNOPRIV;
			
			/* set the authentication method to MD5 */
			session.securityAuthProto = usmHMACMD5AuthProtocol;
			session.securityAuthProtoLen = USM_AUTH_PROTO_MD5_LEN;
			session.securityAuthKeyLen = USM_AUTH_KU_LEN;

			if (generate_Ku(session.securityAuthProto,
				session.securityAuthProtoLen,
				(u_char *) item->snmpv3_authpassphrase, strlen(item->snmpv3_authpassphrase),
				session.securityAuthKey,
				&session.securityAuthKeyLen) != SNMPERR_SUCCESS)
			{
				zbx_snprintf(error,sizeof(error),"Error generating Ku from authentication pass phrase.");

				zabbix_log( LOG_LEVEL_ERR, "%s", error);
				SET_MSG_RESULT(value, strdup(error));

				return NOTSUPPORTED;
			}
		}
		else if(item->snmpv3_securitylevel == ITEM_SNMPV3_SECURITYLEVEL_AUTHPRIV)
		{
			session.securityLevel = SNMP_SEC_LEVEL_AUTHPRIV;

			/* set the authentication method to MD5 */
			session.securityAuthProto = usmHMACMD5AuthProtocol;
			session.securityAuthProtoLen = USM_AUTH_PROTO_MD5_LEN;
			session.securityAuthKeyLen = USM_AUTH_KU_LEN;

			if (generate_Ku(session.securityAuthProto,
				session.securityAuthProtoLen,
				(u_char *) item->snmpv3_authpassphrase, strlen(item->snmpv3_authpassphrase),
				session.securityAuthKey,
				&session.securityAuthKeyLen) != SNMPERR_SUCCESS)
			{
				zbx_snprintf(error,sizeof(error),"Error generating Ku from authentication pass phrase.");

				zabbix_log( LOG_LEVEL_ERR, "%s", error);
				SET_MSG_RESULT(value, strdup(error));

				return NOTSUPPORTED;
			}
			
			/* set the private method to DES */
			session.securityPrivProto = usmDESPrivProtocol;
    			session.securityPrivProtoLen = USM_PRIV_PROTO_DES_LEN;
			session.securityPrivKeyLen = USM_PRIV_KU_LEN;
			
			if (generate_Ku(session.securityAuthProto,
				session.securityAuthProtoLen,
		                (u_char *) item->snmpv3_privpassphrase, strlen(item->snmpv3_privpassphrase),
				session.securityPrivKey,
				&session.securityPrivKeyLen) != SNMPERR_SUCCESS) 
			{
				zbx_snprintf(error,sizeof(error),"Error generating Ku from priv pass phrase.");

				zabbix_log( LOG_LEVEL_ERR, "%s", error);
				SET_MSG_RESULT(value, strdup(error));

				return NOTSUPPORTED;
			}
		}
		zabbix_log( LOG_LEVEL_DEBUG, "SNMPv3 [%s@%s:%d]",
			session.securityName,
			session.peername,
			session.remote_port);
	}
	else
	{
		zbx_snprintf(error,sizeof(error),"Error in get_value_SNMP. Unsupported session.version [%d]",
			(int)session.version);
		zabbix_log( LOG_LEVEL_ERR, "%s",
			error);
		SET_MSG_RESULT(value, strdup(error));
		
		return NOTSUPPORTED;
	}

	zabbix_log( LOG_LEVEL_DEBUG, "OID [%s]",
		snmp_oid);

	if (NULL != CONFIG_SOURCE_IP)
		session.localname = CONFIG_SOURCE_IP;

	SOCK_STARTUP;
	ss = snmp_open(&session);

	if(ss == NULL)
	{
		SOCK_CLEANUP;

		zbx_snprintf(error,sizeof(error),"Error doing snmp_open()");
		zabbix_log( LOG_LEVEL_ERR, "%s",
			error);
		SET_MSG_RESULT(value, strdup(error));

		return NOTSUPPORTED;
	}
	zabbix_log( LOG_LEVEL_DEBUG, "In get_value_SNMP() 0.2");

	pdu = snmp_pdu_create(SNMP_MSG_GET);
/* Changed to snmp_parse_oid */
/* read_objid(item->snmp_oid, anOID, &anOID_len);*/
	snmp_parse_oid(snmp_oid, anOID, &anOID_len);

#if OTHER_METHODS
	get_node("sysDescr.0", anOID, &anOID_len);
	read_objid(".1.3.6.1.2.1.1.1.0", anOID, &anOID_len);
	read_objid("system.sysDescr.0", anOID, &anOID_len);
#endif

	snmp_add_null_var(pdu, anOID, anOID_len);
	zabbix_log( LOG_LEVEL_DEBUG, "In get_value_SNMP() 0.3");
  
	status = snmp_synch_response(ss, pdu, &response);
	zabbix_log( LOG_LEVEL_DEBUG, "Status send [%d]", status);
	zabbix_log( LOG_LEVEL_DEBUG, "In get_value_SNMP() 0.4");

	zabbix_log( LOG_LEVEL_DEBUG, "In get_value_SNMP() 1");

	if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR)
	{

	zabbix_log( LOG_LEVEL_DEBUG, "In get_value_SNMP() 2");
/*		for(vars = response->variables; vars; vars = vars->next_variable)
		{
			print_variable(vars->name, vars->name_length, vars);
		}*/

		for(vars = response->variables; vars; vars = vars->next_variable)
		{
			int count=1;
			zabbix_log( LOG_LEVEL_DEBUG, "AV loop(%d)", vars->type);

/*			if(	(vars->type == ASN_INTEGER) ||*/
			if(	(vars->type == ASN_UINTEGER)||
				(vars->type == ASN_COUNTER) ||
#ifdef OPAQUE_SPECIAL_TYPES
				(vars->type == ASN_UNSIGNED64) ||
#endif
				(vars->type == ASN_TIMETICKS) ||
				(vars->type == ASN_GAUGE)
			)
			{
/*				*result=(long)*vars->val.integer;*/
				/*
				 * This solves situation when large numbers are stored as negative values
				 * http://sourceforge.net/tracker/index.php?func=detail&aid=700145&group_id=23494&atid=378683
				 */ 
				/*zbx_snprintf(result_str,sizeof(result_str),"%ld",(long)*vars->val.integer);*/
/*				zbx_snprintf(result_str,sizeof(result_str),"%lu",(long)*vars->val.integer);*/

				/* Not correct. Returns huge values. */
/*				SET_UI64_RESULT(value, (zbx_uint64_t)*vars->val.integer);*/
				SET_UI64_RESULT(value, (unsigned long)*vars->val.integer);
				zabbix_log( LOG_LEVEL_DEBUG, "OID [%s] Type [%d] UI64[" ZBX_FS_UI64 "]",
					snmp_oid,
					vars->type,
					(zbx_uint64_t)*vars->val.integer);
				zabbix_log( LOG_LEVEL_DEBUG, "OID [%s] Type [%d] ULONG[%lu]",
					snmp_oid,
					vars->type,
					(zbx_uint64_t)(unsigned long)*vars->val.integer);
			}
			else if(vars->type == ASN_COUNTER64)
			{
				/* Incorrect code for 32 bit platforms */
/*				SET_UI64_RESULT(value, ((vars->val.counter64->high)<<32)+(vars->val.counter64->low));*/
				SET_UI64_RESULT(value, (((zbx_uint64_t)vars->val.counter64->high)<<32)+((zbx_uint64_t)vars->val.counter64->low));
			}
			else if(vars->type == ASN_INTEGER
#define ASN_FLOAT           (ASN_APPLICATION | 8)
#define ASN_DOUBLE          (ASN_APPLICATION | 9)

#ifdef OPAQUE_SPECIAL_TYPES
				|| (vars->type == ASN_INTEGER64)
#endif
			)
			{
				/* Negative integer values are converted to double */
				if(*vars->val.integer<0)
				{
					SET_DBL_RESULT(value, (double)*vars->val.integer);
				}
				else
				{
					SET_UI64_RESULT(value, (zbx_uint64_t)*vars->val.integer);
				}
			}
#ifdef OPAQUE_SPECIAL_TYPES
			else if(vars->type == ASN_FLOAT)
			{
				SET_DBL_RESULT(value, *vars->val.floatVal);
			}
			else if(vars->type == ASN_DOUBLE)
			{
				SET_DBL_RESULT(value, *vars->val.doubleVal);
			}
#endif
			else if(vars->type == ASN_OCTET_STR)
			{
				if(item->value_type == ITEM_VALUE_TYPE_FLOAT)
				{
					SET_DBL_RESULT(value, strtod((char*)vars->val.string,0));
				}
				else if(item->value_type != ITEM_VALUE_TYPE_STR)
				{
					zbx_snprintf(error,sizeof(error),"Cannot store SNMP string value (ASN_OCTET_STR) in item having numeric type");
					zabbix_log( LOG_LEVEL_ERR, "%s",
						error);
					SET_MSG_RESULT(value, strdup(error));

					ret = NOTSUPPORTED;
				}
				else
				{
					zabbix_log( LOG_LEVEL_DEBUG, "ASN_OCTET_STR [%s]", vars->val.string);
					zabbix_log( LOG_LEVEL_DEBUG, "ASN_OCTET_STR [%d]", vars->val_len);
					
					p = malloc(1024);
					if(p)
					{
						memset(p,0,1024);
						snprint_value(p, 1023, vars->name, vars->name_length, vars);
						/* Skip STRING: and STRING_HEX: */
						c=strchr(p,':');
						if(c==NULL)
						{
							SET_STR_RESULT(value, strdup(p));
						}
						else
						{
							SET_STR_RESULT(value, strdup(c+1));
						}
						zabbix_log( LOG_LEVEL_DEBUG, "ASN_OCTET_STR [%s]", p);
						free(p);
					}
					else
					{
						zbx_snprintf(error,MAX_STRING_LEN-1,"Cannot allocate required memory");
						zabbix_log( LOG_LEVEL_ERR, "%s", error);
						SET_MSG_RESULT(value, strdup(error));
					}

/*					p = malloc(vars->val_len+1);
					if(p)
					{
						zabbix_log( LOG_LEVEL_WARNING, "Result [%s] len [%d]",vars->val.string,vars->val_len);
						memcpy(p, vars->val.string, vars->val_len);
						p[vars->val_len] = '\0';

						SET_STR_RESULT(value, p);
					}
					else
					{
						zbx_snprintf(error,sizeof(error),"Cannot allocate required memory");
						zabbix_log( LOG_LEVEL_ERR, "%s", error);
						SET_MSG_RESULT(value, strdup(error));
					}*/
				}
			}
			else if(vars->type == ASN_IPADDRESS)
			{
				if(item->value_type != ITEM_VALUE_TYPE_STR)
				{
					zbx_snprintf(error,sizeof(error),"Cannot store SNMP string value (ASN_IPADDRESS) in item having numeric type");
					zabbix_log( LOG_LEVEL_ERR, "%s",
						error);
					SET_MSG_RESULT(value, strdup(error));
					ret = NOTSUPPORTED;
				}
				else
				{
					ip = vars->val.string;
					SET_STR_RESULT(value, zbx_dsprintf(NULL, "%d.%d.%d.%d",
						ip[0],
						ip[1],
						ip[2],
						ip[3]));
				}
			}
			else
			{
/* count is not really used. Has to be removed */ 
				count++;

				zbx_snprintf(error,sizeof(error),"OID [%s] value #%d has unknow type [%X]",
					snmp_oid,
					count,
					vars->type);

				zabbix_log( LOG_LEVEL_ERR, "%s",
					error);
				SET_MSG_RESULT(value, strdup(error));

				ret  = NOTSUPPORTED;
			}
		}
	}
	else
	{
		if (status == STAT_SUCCESS)
		{
			zabbix_log( LOG_LEVEL_WARNING, "SNMP error in packet. Reason: %s\n",
				snmp_errstring(response->errstat));
			if(response->errstat == SNMP_ERR_NOSUCHNAME)
			{
				zbx_snprintf(error,sizeof(error),"SNMP error [%s]",
					snmp_errstring(response->errstat));

				zabbix_log( LOG_LEVEL_ERR, "%s",
					error);
				SET_MSG_RESULT(value, strdup(error));

				ret=NOTSUPPORTED;
			}
			else
			{
				zbx_snprintf(error,sizeof(error),"SNMP error [%s]",
					snmp_errstring(response->errstat));

				zabbix_log( LOG_LEVEL_ERR, "%s",
					error);
				SET_MSG_RESULT(value, strdup(error));

				ret=NOTSUPPORTED;
			}
		}
		else if(status == STAT_TIMEOUT)
		{
			zbx_snprintf(error,sizeof(error),"Timeout while connecting to [%s]",
				session.peername);

/*			snmp_sess_perror("snmpget", ss);*/
			zabbix_log( LOG_LEVEL_ERR, "%s",
				error);
			SET_MSG_RESULT(value, strdup(error));

			ret = NETWORK_ERROR;
		}
		else
		{
			zbx_snprintf(error,sizeof(error),"SNMP error [%d]",
				status);

			zabbix_log( LOG_LEVEL_ERR, "%s",
				error);
			SET_MSG_RESULT(value, strdup(error));

			ret=NOTSUPPORTED;
		}
	}

	if (response)
	{
		snmp_free_pdu(response);
	}
	snmp_close(ss);

	SOCK_CLEANUP;
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: snmp_normalize                                                   *
 *                                                                            *
 * Purpose:  translate well known MIBs into numerics                          *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static void snmp_normalize(char *buf, char *oid, int maxlen)
{
#define ZBX_MIB_NORM struct zbx_mib_norm_t

ZBX_MIB_NORM
{
	char	*mib;
	char	*replace;
};

static ZBX_MIB_NORM mibs[]=
{
	{"ifIndex",		"1.3.6.1.2.1.2.2.1.1"},
	{"ifDescr",		"1.3.6.1.2.1.2.2.1.2"},
	{"ifType",		"1.3.6.1.2.1.2.2.1.3"},
	{"ifMtu",		"1.3.6.1.2.1.2.2.1.4"},
	{"ifSpeed",		"1.3.6.1.2.1.2.2.1.5"},
	{"ifPhysAddress",	"1.3.6.1.2.1.2.2.1.6"},
	{"ifAdminStatus",	"1.3.6.1.2.1.2.2.1.7"},
	{"ifOperStatus",	"1.3.6.1.2.1.2.2.1.8"},
	{"ifInOctets",		"1.3.6.1.2.1.2.2.1.10"},
	{"ifInUcastPkts",	"1.3.6.1.2.1.2.2.1.11"},
	{"ifInNUcastPkts",	"1.3.6.1.2.1.2.2.1.12"},
	{"ifInDiscards",	"1.3.6.1.2.1.2.2.1.13"},
	{"ifInErrors",		"1.3.6.1.2.1.2.2.1.14"},
	{"ifInUnknownProtos",	"1.3.6.1.2.1.2.2.1.15"},
	{"ifOutOctets",		"1.3.6.1.2.1.2.2.1.17"},
	{"ifOutNUcastPkts",	"1.3.6.1.2.1.2.2.1.18"},
	{"ifOutDiscards",	"1.3.6.1.2.1.2.2.1.19"},
	{"ifOutErrors",		"1.3.6.1.2.1.2.2.1.20"},
	{"ifOutQLen",		"1.3.6.1.2.1.2.2.1.21"},
	{NULL}
};
	int found = 0;
	int i;

	zabbix_log( LOG_LEVEL_DEBUG, "In snmp_normalize(oid:%s)",
		oid);

	for(i=0;mibs[i].mib!=NULL;i++)
	{
		if(strncmp(mibs[i].mib,oid,strlen(mibs[i].mib)) == 0)
		{
			found = 1;
			if(strlen(mibs[i].mib) == strlen(oid))
			{
				zbx_strlcpy(buf, mibs[i].replace, maxlen);
			}
			else
			{
				zbx_snprintf(buf, maxlen, "%s%s",
					mibs[i].replace,
					oid+strlen(mibs[i].mib));
			}
			break;
		} 
	}
	if(0 == found)
	{
		zbx_strlcpy(buf, oid, maxlen);
	}

	zabbix_log( LOG_LEVEL_DEBUG, "End of snmp_normalize(result:%s)",
		buf);
}

int	get_value_snmp(DB_ITEM *item, AGENT_RESULT *value)
{
	int	ret = SUCCEED;
	char	error[MAX_STRING_LEN];
	char	method[MAX_STRING_LEN];
	char	oid_normalized[MAX_STRING_LEN];
	char	oid_index[MAX_STRING_LEN];
	char	oid_full[MAX_STRING_LEN];
	char	index_value[MAX_STRING_LEN];
	int	idx;
	char	*pl;
	int	num;

	zabbix_log( LOG_LEVEL_DEBUG, "In get_value_snmp(key:%s, oid:%s)",
		item->key,
		item->snmp_oid);

	num = num_key_param(item->snmp_oid);

	switch (num)
	{
	case 0:
		zabbix_log( LOG_LEVEL_DEBUG,"Standard processing");
		snmp_normalize(oid_normalized, item->snmp_oid, sizeof(oid_normalized));
		ret = get_snmp(item, oid_normalized, value);
		break;
	case 3:
		do {
			zabbix_log( LOG_LEVEL_DEBUG,"Special processing");
			oid_index[0]='\0';
			method[0]='\0';
			index_value[0]='\0';
			if(get_key_param(item->snmp_oid, 1, method, MAX_STRING_LEN) != 0
				|| get_key_param(item->snmp_oid, 2, oid_index, MAX_STRING_LEN) != 0
				|| get_key_param(item->snmp_oid, 3, index_value, MAX_STRING_LEN) != 0
			)
			{
				zbx_snprintf(error,sizeof(error),"Cannot retrieve all three parameters from [%s]",
					item->snmp_oid);
	
				zabbix_log( LOG_LEVEL_ERR, "%s",
					error);
				SET_MSG_RESULT(value, strdup(error));
	
				ret  = NOTSUPPORTED;
				break;
			}
			zabbix_log( LOG_LEVEL_DEBUG,"method:%s", method);
			zabbix_log( LOG_LEVEL_DEBUG,"oid_index:%s", oid_index);
			zabbix_log( LOG_LEVEL_DEBUG,"index_value:%s", index_value);
			if(strcmp("index", method) != 0)
			{
				zbx_snprintf(error,sizeof(error),"Unsupported method [%s] in the OID [%s]",
					method,
					item->snmp_oid);
	
				zabbix_log( LOG_LEVEL_ERR, "%s",
					error);
				SET_MSG_RESULT(value, strdup(error));
	
				ret  = NOTSUPPORTED;
				break;
			}
			snmp_normalize(oid_normalized, oid_index, sizeof(oid_normalized));
			if(snmp_get_index (item, oid_normalized, index_value, &idx) != SUCCEED)
			{
				zbx_snprintf(error,sizeof(error),"Cannot find index [%s] of the OID [%s]",
					oid_index,
					item->snmp_oid);
	
				zabbix_log( LOG_LEVEL_ERR, "%s",
					error);
				SET_MSG_RESULT(value, strdup(error));
	
				ret  = NOTSUPPORTED;
				break;
			}
			
			zabbix_log( LOG_LEVEL_DEBUG,"Found index:%d", idx);
			pl=strchr(item->snmp_oid,'[');
			if(NULL == pl)
			{
				zbx_snprintf(error,sizeof(error),"Cannot find left bracket in the OID [%s]",
					item->snmp_oid);
	
				zabbix_log( LOG_LEVEL_ERR, "%s",
					error);
				SET_MSG_RESULT(value, strdup(error));
	
				ret  = NOTSUPPORTED;
				break;
			}
			pl[0]='\0';
			snmp_normalize(oid_normalized, item->snmp_oid, sizeof(oid_normalized));
			zbx_snprintf(oid_full, sizeof(oid_full), "%s.%d",
				oid_normalized,
				idx);
			zabbix_log( LOG_LEVEL_DEBUG,"Full OID:%s", oid_full);
			ret = get_snmp(item, oid_full, value);
			pl[0]='[';
		} while(0);
		break;
	default:
		zbx_snprintf(error,sizeof(error),"OID [%s] contains unsupported parameters",
			item->snmp_oid);

		zabbix_log( LOG_LEVEL_ERR, "%s",
			error);
		SET_MSG_RESULT(value, strdup(error));

		ret = NOTSUPPORTED;
	}

	return ret;
}

#endif
