/*
 * Copyright (C) 2003 Mathias Brossard <mathias.brossard@idealx.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307,
 * USA
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <opensc/opensc.h>
#include "pkcs11-display.h"

#define __PASTE(x,y)      x##y

/* Declare all spy_* Cryptoki function */

#define CK_NEED_ARG_LIST  1
#define CK_PKCS11_FUNCTION_INFO(name) CK_RV name

#include "rsaref/pkcs11f.h"

/* Spy Module Function List */
CK_FUNCTION_LIST_PTR pkcs11_spy = NULL;
/* Real Module Function List */
CK_FUNCTION_LIST_PTR po = NULL;
/* Dynamic Module Handle */
static void *modhandle = NULL;
/* Spy module output */
FILE *spy_output = NULL;

#undef CK_NEED_ARG_LIST
#undef CK_PKCS11_FUNCTION_INFO
#define CK_PKCS11_FUNCTION_INFO(name) \
    pkcs11_spy->name = name;

/* Inits the spy. If successfull, po != NULL */
CK_RV init_spy(void)
{
  const char *mspec = NULL, *file = NULL, *env = NULL;
  scconf_block *conf_block = NULL, **blocks;
  struct sc_context *ctx = NULL;
  int rv = CKR_OK, r, i;

  /* Allocates and initializes the pkcs11_spy structure */
  pkcs11_spy =
    (CK_FUNCTION_LIST_PTR) malloc(sizeof(CK_FUNCTION_LIST));
  if (pkcs11_spy) {
#include "rsaref/pkcs11f.h"
  } else {
    return CKR_HOST_MEMORY;
  }

  r = sc_establish_context(&ctx, "pkcs11-spy");
  if (r != 0) {
    free(pkcs11_spy);
    return CKR_HOST_MEMORY;
  }

  for (i = 0; ctx->conf_blocks[i] != NULL; i++) {
    blocks = scconf_find_blocks(ctx->conf, ctx->conf_blocks[i],
      "spy", NULL);
    conf_block = blocks[0];
    free(blocks);
    if (conf_block != NULL)
      break;
  }

  /* If conf_block is NULL, just return the default value
   *
   * Don't use getenv() as the last parameter for scconf_get_str(),
   * as we want to be able to override configuration file via
   * environment variables
   */
  env = getenv("PKCS11SPY_OUTPUT");
  file = env ? env : scconf_get_str(conf_block, "output", NULL);
  if (file) {
    spy_output = fopen(file, "a");
  }
  if (!spy_output) {
    spy_output = stderr;
  }
  fprintf(spy_output, "\n\n*************** OpenSC PKCS#11 spy *****************\n");

  env = getenv("PKCS11SPY");
  mspec = env ? env : scconf_get_str(conf_block, "module", NULL);
  modhandle = C_LoadModule(mspec, &po);
  if (modhandle && po) {
    fprintf(spy_output, "Loaded: \"%s\"\n", mspec == NULL ? "default module" : mspec);
  } else {
  	po = NULL;
  	free(pkcs11_spy);
  	rv = CKR_GENERAL_ERROR;
  }
  sc_release_context(ctx);
  return rv;
}

void enter(char *function)
{
  static int count = 0;
  fprintf(spy_output, "\n\n%d: %s\n", count++, function);
}

CK_RV retne(CK_RV rv)
{
	fprintf(spy_output, "Returned:  %ld %s\n", rv, lookup_enum ( RV_T, rv ));
	fflush(spy_output);
	return rv;
}

void spy_dump_string_in(char *name, CK_VOID_PTR data, CK_ULONG size)
{
  fprintf(spy_output, "[in] %s ", name);
  print_generic(spy_output, 0, data, size, NULL);
}

void spy_dump_string_out(char *name, CK_VOID_PTR data, CK_ULONG size)
{
  fprintf(spy_output, "[out] %s ", name);
  print_generic(spy_output, 0, data, size, NULL);
}

void spy_dump_ulong_in(char *name, CK_ULONG value)
{
  fprintf(spy_output, "[in] %s = 0x%lx\n", name, value);
}

void spy_dump_ulong_out(char *name, CK_ULONG value)
{
  fprintf(spy_output, "[out] %s = 0x%lx\n", name, value);
}

void spy_dump_desc_out(char *name)
{
  fprintf(spy_output, "[out] %s: \n", name);
}

void spy_dump_array_out(char *name, CK_ULONG size)
{
  fprintf(spy_output, "[out] %s[%ld]: \n", name, size);
}

void spy_attribute_req_in(char *name, CK_ATTRIBUTE_PTR pTemplate,
			  CK_ULONG  ulCount)
{
  fprintf(spy_output, "[in] %s[%ld]: \n", name, ulCount);
  print_attribute_list_req(spy_output, pTemplate, ulCount);
}

void spy_attribute_list_in(char *name, CK_ATTRIBUTE_PTR pTemplate,
			  CK_ULONG  ulCount)
{
  fprintf(spy_output, "[in] %s[%ld]: \n", name, ulCount);
  print_attribute_list(spy_output, pTemplate, ulCount);
}

void spy_attribute_list_out(char *name, CK_ATTRIBUTE_PTR pTemplate,
			  CK_ULONG  ulCount)
{
  fprintf(spy_output, "[out] %s[%ld]: \n", name, ulCount);
  print_attribute_list(spy_output, pTemplate, ulCount);
}

CK_RV C_GetFunctionList
(CK_FUNCTION_LIST_PTR_PTR ppFunctionList)
{
  if (po == NULL) {
    CK_RV rv = init_spy();
    if (rv != CKR_OK)
    	return rv;
  }

  enter("C_GetFunctionList");
  *ppFunctionList = pkcs11_spy;
  return retne(CKR_OK);
}

CK_RV C_Initialize(CK_VOID_PTR pInitArgs)
{
  CK_RV rv;

  if (po == NULL) {
    rv = init_spy();
    if (rv != CKR_OK)
    	return rv;
  }

  enter("C_Initialize");
  rv = po->C_Initialize(pInitArgs);
  return retne(rv);
}

CK_RV C_Finalize(CK_VOID_PTR pReserved)
{
  CK_RV rv;
  enter("C_Finalize");
  rv = po->C_Finalize(pReserved);
  /* After Finalize do not use the module again */
  C_UnloadModule(modhandle);
  po = NULL;
  return retne(rv);
}

CK_RV C_GetInfo(CK_INFO_PTR pInfo)
{
  CK_RV rv;
  enter("C_GetInfo");
  rv = po->C_GetInfo(pInfo);
  if(rv == CKR_OK) {
    print_ck_info(spy_output, pInfo);
  }
  return retne(rv);
}

CK_RV C_GetSlotList(CK_BBOOL tokenPresent,
			CK_SLOT_ID_PTR pSlotList,
			CK_ULONG_PTR pulCount)
{
  CK_RV rv;
  enter("C_GetSlotList");
  spy_dump_ulong_in("tokenPresent", tokenPresent);
  rv = po->C_GetSlotList(tokenPresent, pSlotList, pulCount);
  if(rv == CKR_OK) {
    spy_dump_desc_out("pSlotList");
    print_slot_list(spy_output, pSlotList, *pulCount);
    spy_dump_ulong_out("*pulCount", *pulCount);
  }
  return retne(rv);
}

CK_RV C_GetSlotInfo(CK_SLOT_ID slotID,
			CK_SLOT_INFO_PTR pInfo)
{
  CK_RV rv;
  enter("C_GetSlotInfo");
  spy_dump_ulong_in("slotID", slotID);
  rv = po->C_GetSlotInfo(slotID, pInfo);
  if(rv == CKR_OK) {
    spy_dump_desc_out("pInfo");
    print_slot_info(spy_output, pInfo);
  }
  return retne(rv);
}

CK_RV C_GetTokenInfo(CK_SLOT_ID slotID,
			 CK_TOKEN_INFO_PTR pInfo)
{
  CK_RV rv;
  enter("C_GetTokenInfo");
  spy_dump_ulong_in("slotID", slotID);
  rv = po->C_GetTokenInfo(slotID, pInfo);
  if(rv == CKR_OK) {
    spy_dump_desc_out("pInfo");
    print_token_info(spy_output, pInfo);
  }
  return retne(rv);
}

CK_RV C_GetMechanismList(CK_SLOT_ID  slotID,
			     CK_MECHANISM_TYPE_PTR pMechanismList,
			     CK_ULONG_PTR  pulCount)
{
  CK_RV rv;
  enter("C_GetMechanismList");
  spy_dump_ulong_in("slotID", slotID);
  rv = po->C_GetMechanismList(slotID, pMechanismList, pulCount);
  if(rv == CKR_OK) {
    spy_dump_array_out("pMechanismList", *pulCount);
    print_mech_list(spy_output, pMechanismList, *pulCount);
  }
  return retne(rv);
}

CK_RV C_GetMechanismInfo(CK_SLOT_ID  slotID,
			     CK_MECHANISM_TYPE type,
			     CK_MECHANISM_INFO_PTR pInfo)
{
  CK_RV rv;
  const char *name = lookup_enum(MEC_T, type);
  enter("C_GetMechanismInfo");
  spy_dump_ulong_in("slotID", slotID);
  if (name) {
    fprintf(spy_output, "%30s \n", name);
  } else {
    fprintf(spy_output, " Unknown Mechanism (%08lx)  \n", type);
  }
  rv = po->C_GetMechanismInfo(slotID, type, pInfo);
  if(rv == CKR_OK) {
    spy_dump_desc_out("pInfo");
    print_mech_info(spy_output, type, pInfo);
  }
  return retne(rv);
}

CK_RV C_InitToken (CK_SLOT_ID slotID,
		       CK_UTF8CHAR_PTR pPin,
		       CK_ULONG ulPinLen,
		       CK_UTF8CHAR_PTR pLabel)
{
  CK_RV rv;
  enter("C_InitToken");
  spy_dump_ulong_in("slotID", slotID);
  spy_dump_string_in("pPin[ulPinLen]", pPin, ulPinLen);
  spy_dump_string_in("pLabel[32]", pLabel, 32);
  rv = po->C_InitToken (slotID, pPin, ulPinLen, pLabel);
  return retne(rv);
}

CK_RV C_InitPIN(CK_SESSION_HANDLE hSession,
		    CK_UTF8CHAR_PTR pPin,
		    CK_ULONG  ulPinLen)
{
  CK_RV rv;
  enter("C_InitPIN");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pPin[ulPinLen]", pPin, ulPinLen);
  rv = po->C_InitPIN(hSession, pPin, ulPinLen);
  return retne(rv);
}

CK_RV C_SetPIN(CK_SESSION_HANDLE hSession,
		   CK_UTF8CHAR_PTR pOldPin,
		   CK_ULONG  ulOldLen,
		   CK_UTF8CHAR_PTR pNewPin,
		   CK_ULONG  ulNewLen)
{
  CK_RV rv;
  enter("C_SetPIN");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pOldPin[ulOldLen]", pOldPin, ulOldLen);
  spy_dump_string_in("pNewPin[ulNewLen]", pNewPin, ulNewLen);
  rv = po->C_SetPIN(hSession, pOldPin, ulOldLen,
		    pNewPin, ulNewLen);
  return retne(rv);
}

CK_RV C_OpenSession(CK_SLOT_ID  slotID,
			CK_FLAGS  flags,
			CK_VOID_PTR  pApplication,
			CK_NOTIFY  Notify,
			CK_SESSION_HANDLE_PTR phSession)
{
  CK_RV rv;
  enter("C_OpenSession");
  spy_dump_ulong_in("slotID", slotID);
  spy_dump_ulong_in("flags", flags);
  fprintf(spy_output, "pApplication=%p\n", pApplication);
  fprintf(spy_output, "Notify=%p\n", (void *)Notify);
  rv = po->C_OpenSession(slotID, flags, pApplication,
			 Notify, phSession);
  spy_dump_ulong_out("*phSession", *phSession);
  return retne(rv);
}


CK_RV C_CloseSession(CK_SESSION_HANDLE hSession)
{
  CK_RV rv;
  enter("C_CloseSession");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_CloseSession(hSession);
  return retne(rv);
}


CK_RV C_CloseAllSessions(CK_SLOT_ID slotID)
{
  CK_RV rv;
  enter("C_CloseAllSessions");
  spy_dump_ulong_in("slotID", slotID);
  rv = po->C_CloseAllSessions(slotID);
  return retne(rv);
}


CK_RV C_GetSessionInfo(CK_SESSION_HANDLE hSession,
			   CK_SESSION_INFO_PTR pInfo)
{
  CK_RV rv;
  enter("C_GetSessionInfo");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_GetSessionInfo(hSession, pInfo);
  if(rv == CKR_OK) {
    spy_dump_desc_out("pInfo");
    print_session_info(spy_output, pInfo);
  }
  return retne(rv);
}


CK_RV C_GetOperationState(CK_SESSION_HANDLE hSession,
			      CK_BYTE_PTR pOperationState,
			      CK_ULONG_PTR pulOperationStateLen)
{
  CK_RV rv;
  enter("C_GetOperationState");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_GetOperationState(hSession, pOperationState,
			       pulOperationStateLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pOperationState[*pulOperationStateLen]",
			pOperationState, *pulOperationStateLen);
  }
  return retne(rv);
}


CK_RV C_SetOperationState(CK_SESSION_HANDLE hSession,
			      CK_BYTE_PTR pOperationState,
			      CK_ULONG  ulOperationStateLen,
			      CK_OBJECT_HANDLE hEncryptionKey,
			      CK_OBJECT_HANDLE hAuthenticationKey)
{
  CK_RV rv;
  enter("SetOperationState");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pOperationState[ulOperationStateLen]",
		     pOperationState, ulOperationStateLen);
  spy_dump_ulong_in("hEncryptionKey", hEncryptionKey);
  spy_dump_ulong_in("hAuthenticationKey", hAuthenticationKey);
  rv = po->C_SetOperationState(hSession, pOperationState,
			       ulOperationStateLen,
			       hEncryptionKey,
			       hAuthenticationKey);
  return retne(rv);
}


CK_RV C_Login(CK_SESSION_HANDLE hSession,
		  CK_USER_TYPE userType,
		  CK_UTF8CHAR_PTR pPin,
		  CK_ULONG  ulPinLen)
{
  CK_RV rv;
  enter("C_Login");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "[in] userType = %s\n",
	  lookup_enum(USR_T, userType));
  spy_dump_string_in("pPin[ulPinLen]", pPin, ulPinLen);
  rv = po->C_Login(hSession, userType, pPin, ulPinLen);
  return retne(rv);
}

CK_RV C_Logout(CK_SESSION_HANDLE hSession)
{
  CK_RV rv;
  enter("C_Logout");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_Logout(hSession);
  return retne(rv);
}

CK_RV C_CreateObject(CK_SESSION_HANDLE hSession,
			 CK_ATTRIBUTE_PTR pTemplate,
			 CK_ULONG  ulCount,
			 CK_OBJECT_HANDLE_PTR phObject)
{
  CK_RV rv;
  enter("C_CreateObject");
  spy_dump_ulong_in("hSession", hSession);
  spy_attribute_list_in("pTemplate", pTemplate, ulCount);
  rv = po->C_CreateObject(hSession, pTemplate, ulCount, phObject);
  if (rv == CKR_OK) {
    spy_dump_ulong_out("*phObject", *phObject);
  }
  return retne(rv);
}

CK_RV C_CopyObject(CK_SESSION_HANDLE hSession,
		       CK_OBJECT_HANDLE hObject,
		       CK_ATTRIBUTE_PTR pTemplate,
		       CK_ULONG  ulCount,
		       CK_OBJECT_HANDLE_PTR phNewObject)
{
  CK_RV rv;
  enter("C_CopyObject");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_ulong_in("hObject", hObject);
  spy_attribute_list_in("pTemplate", pTemplate, ulCount);
  rv = po->C_CopyObject(hSession, hObject, pTemplate, ulCount, phNewObject);
  if (rv == CKR_OK) {
    spy_dump_ulong_out("*phNewObject", *phNewObject);
  }
  return retne(rv);
}


CK_RV C_DestroyObject(CK_SESSION_HANDLE hSession,
			  CK_OBJECT_HANDLE hObject)
{
  CK_RV rv;
  enter("C_DestroyObject");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_ulong_in("hObject", hObject);
  rv = po->C_DestroyObject(hSession, hObject);
  return retne(rv);
}


CK_RV C_GetObjectSize(CK_SESSION_HANDLE hSession,
			  CK_OBJECT_HANDLE hObject,
			  CK_ULONG_PTR pulSize)
{
  CK_RV rv;
  enter("C_GetObjectSize");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_ulong_in("hObject", hObject);
  rv = po->C_GetObjectSize(hSession, hObject, pulSize);
  if (rv == CKR_OK) {
    spy_dump_ulong_out("*pulSize", *pulSize);
  }
  return retne(rv);
}


CK_RV C_GetAttributeValue(CK_SESSION_HANDLE hSession,
			      CK_OBJECT_HANDLE hObject,
			      CK_ATTRIBUTE_PTR pTemplate,
			      CK_ULONG  ulCount)
{
  CK_RV rv;
  enter("C_GetAttributeValue");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_ulong_in("hObject", hObject);
  spy_attribute_req_in("pTemplate", pTemplate, ulCount);
  rv = po->C_GetAttributeValue(hSession, hObject, pTemplate, ulCount);
  if (rv == CKR_OK) {
    spy_attribute_list_out("pTemplate", pTemplate, ulCount);
  }
  return retne(rv);
}


CK_RV C_SetAttributeValue(CK_SESSION_HANDLE hSession,
			      CK_OBJECT_HANDLE hObject,
			      CK_ATTRIBUTE_PTR pTemplate,
			      CK_ULONG  ulCount)
{
  CK_RV rv;
  enter("C_SetAttributeValue");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_ulong_in("hObject", hObject);
  spy_attribute_list_in("pTemplate", pTemplate, ulCount);
  rv = po->C_SetAttributeValue(hSession, hObject, pTemplate, ulCount);
  return retne(rv);
}


CK_RV C_FindObjectsInit(CK_SESSION_HANDLE hSession,
			    CK_ATTRIBUTE_PTR pTemplate,
			    CK_ULONG  ulCount)
{
  CK_RV rv;
  enter("C_FindObjectsInit");
  spy_dump_ulong_in("hSession", hSession);
  spy_attribute_list_in("pTemplate", pTemplate, ulCount);
  rv = po->C_FindObjectsInit(hSession, pTemplate, ulCount);
  return retne(rv);
}


CK_RV C_FindObjects(CK_SESSION_HANDLE hSession,
			CK_OBJECT_HANDLE_PTR phObject,
			CK_ULONG  ulMaxObjectCount,
			CK_ULONG_PTR  pulObjectCount)
{
  CK_RV rv;
  enter("C_FindObjects");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_ulong_in("ulMaxObjectCount", ulMaxObjectCount);
  rv = po->C_FindObjects(hSession, phObject, ulMaxObjectCount,
			 pulObjectCount);
  if (rv == CKR_OK) {
    CK_ULONG          i;
    spy_dump_ulong_out("ulObjectCount", *pulObjectCount);
    for (i = 0; i < *pulObjectCount; i++) {
      fprintf(spy_output, "Object %ld Matches\n", phObject[i]);
    }
  }
  return retne(rv);
}


CK_RV C_FindObjectsFinal(CK_SESSION_HANDLE hSession)
{
  CK_RV rv;
  enter("C_FindObjectsFinal");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_FindObjectsFinal(hSession);
  return retne(rv);
}

CK_RV C_EncryptInit(CK_SESSION_HANDLE hSession,
			CK_MECHANISM_PTR pMechanism,
			CK_OBJECT_HANDLE hKey)
{
  CK_RV rv;
  enter("C_EncryptInit");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  spy_dump_ulong_in("hKey", hKey);
  rv = po->C_EncryptInit(hSession, pMechanism, hKey);
  return retne(rv);
}


CK_RV C_Encrypt(CK_SESSION_HANDLE hSession,
		    CK_BYTE_PTR pData,
		    CK_ULONG  ulDataLen,
		    CK_BYTE_PTR pEncryptedData,
		    CK_ULONG_PTR pulEncryptedDataLen)
{
  CK_RV rv;
  enter("C_Encrypt");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pData[ulDataLen]", pData, ulDataLen);
  rv = po->C_Encrypt(hSession, pData, ulDataLen,
		     pEncryptedData, pulEncryptedDataLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pEncryptedData[*pulEncryptedDataLen]",
			pEncryptedData, *pulEncryptedDataLen);
  }
  return retne(rv);
}


CK_RV C_EncryptUpdate(CK_SESSION_HANDLE hSession,
			  CK_BYTE_PTR pPart,
			  CK_ULONG  ulPartLen,
			  CK_BYTE_PTR pEncryptedPart,
			  CK_ULONG_PTR pulEncryptedPartLen)
{
  CK_RV rv;
  enter("C_EncryptUpdate");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pPart[ulPartLen]", pPart, ulPartLen);
  rv = po->C_EncryptUpdate(hSession, pPart, ulPartLen, pEncryptedPart,
			   pulEncryptedPartLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pEncryptedPart[*pulEncryptedPartLen]",
			pEncryptedPart, *pulEncryptedPartLen);
  }
  return retne(rv);
}

CK_RV C_EncryptFinal(CK_SESSION_HANDLE hSession,
			 CK_BYTE_PTR pLastEncryptedPart,
			 CK_ULONG_PTR pulLastEncryptedPartLen)
{
  CK_RV rv;
  enter("C_EncryptFinal");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_EncryptFinal(hSession, pLastEncryptedPart,
			  pulLastEncryptedPartLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pLastEncryptedPart[*pulLastEncryptedPartLen]",
			pLastEncryptedPart, *pulLastEncryptedPartLen);
  }
  return retne(rv);
}


CK_RV C_DecryptInit(CK_SESSION_HANDLE hSession,
			CK_MECHANISM_PTR pMechanism,
			CK_OBJECT_HANDLE hKey)
{
  CK_RV rv;
  enter("C_DecryptInit");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  spy_dump_ulong_in("hKey", hKey);
  rv = po->C_DecryptInit(hSession, pMechanism, hKey);
  return retne(rv);
}


CK_RV C_Decrypt(CK_SESSION_HANDLE hSession,
		    CK_BYTE_PTR pEncryptedData,
		    CK_ULONG  ulEncryptedDataLen,
		    CK_BYTE_PTR pData,
		    CK_ULONG_PTR pulDataLen)
{
  CK_RV rv;
  enter("C_Decrypt");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pEncryptedData[ulEncryptedDataLen]",
		      pEncryptedData, ulEncryptedDataLen);
  rv = po->C_Decrypt(hSession, pEncryptedData, ulEncryptedDataLen,
		     pData, pulDataLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pData[*pulDataLen]", pData, *pulDataLen);
  }
  return retne(rv);
}


CK_RV C_DecryptUpdate(CK_SESSION_HANDLE hSession,
			  CK_BYTE_PTR pEncryptedPart,
			  CK_ULONG  ulEncryptedPartLen,
			  CK_BYTE_PTR pPart,
			  CK_ULONG_PTR pulPartLen)
{
  CK_RV rv;
  enter("C_DecryptUpdate");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pEncryptedPart[ulEncryptedPartLen]",
		      pEncryptedPart, ulEncryptedPartLen);
  rv = po->C_DecryptUpdate(hSession, pEncryptedPart, ulEncryptedPartLen,
			   pPart, pulPartLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pPart[*pulPartLen]", pPart, *pulPartLen);
  }
  return retne(rv);
}


CK_RV C_DecryptFinal(CK_SESSION_HANDLE hSession,
			 CK_BYTE_PTR pLastPart,
			 CK_ULONG_PTR pulLastPartLen)
{
  CK_RV rv;
  enter("C_DecryptFinal");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_DecryptFinal(hSession, pLastPart, pulLastPartLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pLastPart[*pulLastPartLen]",
			pLastPart, *pulLastPartLen);
  }
  return retne(rv);
}

CK_RV C_DigestInit(CK_SESSION_HANDLE hSession,
		       CK_MECHANISM_PTR pMechanism)
{
  CK_RV rv;
  enter("C_DigestInit");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  rv = po->C_DigestInit(hSession, pMechanism);
  return retne(rv);
}


CK_RV C_Digest(CK_SESSION_HANDLE hSession,
		   CK_BYTE_PTR pData,
		   CK_ULONG  ulDataLen,
		   CK_BYTE_PTR pDigest,
		   CK_ULONG_PTR pulDigestLen)
{
  CK_RV rv;
  enter("C_Digest");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pData[ulDataLen]", pData, ulDataLen);
  rv = po->C_Digest(hSession, pData, ulDataLen, pDigest, pulDigestLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pDigest[*pulDigestLen]",
			pDigest, *pulDigestLen);
  }
  return retne(rv);
}


CK_RV C_DigestUpdate(CK_SESSION_HANDLE hSession,
			 CK_BYTE_PTR pPart,
			 CK_ULONG  ulPartLen)
{
  CK_RV rv;
  enter("C_DigestUpdate");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pPart[ulPartLen]", pPart, ulPartLen);
  rv = po->C_DigestUpdate(hSession, pPart, ulPartLen);
  return retne(rv);
}


CK_RV C_DigestKey(CK_SESSION_HANDLE hSession,
		      CK_OBJECT_HANDLE hKey)
{
  CK_RV rv;
  enter("C_DigestKey");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_ulong_in("hKey", hKey);
  rv = po->C_DigestKey(hSession, hKey);
  return retne(rv);
}


CK_RV C_DigestFinal(CK_SESSION_HANDLE hSession,
			CK_BYTE_PTR pDigest,
			CK_ULONG_PTR pulDigestLen)
{
  CK_RV rv;
  enter("C_DigestFinal");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_DigestFinal(hSession, pDigest, pulDigestLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pDigest[*pulDigestLen]",
			pDigest, *pulDigestLen);
  }
  return retne(rv);
}

CK_RV C_SignInit(CK_SESSION_HANDLE hSession,
		     CK_MECHANISM_PTR pMechanism,
		     CK_OBJECT_HANDLE hKey)
{
  CK_RV rv;
  enter("C_SignInit");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  spy_dump_ulong_in("hKey", hKey);
  rv = po->C_SignInit(hSession, pMechanism, hKey);
  return retne(rv);
}


CK_RV C_Sign(CK_SESSION_HANDLE hSession,
		 CK_BYTE_PTR pData,
		 CK_ULONG  ulDataLen,
		 CK_BYTE_PTR pSignature,
		 CK_ULONG_PTR pulSignatureLen)
{
  CK_RV rv;
  enter("C_Sign");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pData[ulDataLen]", pData, ulDataLen);
  rv = po->C_Sign(hSession, pData, ulDataLen, pSignature, pulSignatureLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pSignature[*pulSignatureLen]",
			pSignature, *pulSignatureLen);
  }
  return retne(rv);
}


CK_RV C_SignUpdate(CK_SESSION_HANDLE hSession,
		       CK_BYTE_PTR pPart,
		       CK_ULONG  ulPartLen)
{
  CK_RV rv;
  enter("C_SignUpdate");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pPart[ulPartLen]", pPart, ulPartLen);
  rv = po->C_SignUpdate(hSession, pPart, ulPartLen);
  return retne(rv);
}


CK_RV C_SignFinal(CK_SESSION_HANDLE hSession,
		      CK_BYTE_PTR pSignature,
		      CK_ULONG_PTR pulSignatureLen)
{
  CK_RV rv;
  enter("C_SignFinal");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_SignFinal(hSession, pSignature, pulSignatureLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pSignature[*pulSignatureLen]",
			pSignature, *pulSignatureLen);
  }
  return retne(rv);
}


CK_RV C_SignRecoverInit(CK_SESSION_HANDLE hSession,
			    CK_MECHANISM_PTR pMechanism,
			    CK_OBJECT_HANDLE hKey)
{
  CK_RV rv;
  enter("C_SignRecoverInit");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  spy_dump_ulong_in("hKey", hKey);
  rv = po->C_SignRecoverInit(hSession, pMechanism, hKey);
  return retne(rv);
}


CK_RV C_SignRecover(CK_SESSION_HANDLE hSession,
			CK_BYTE_PTR pData,
			CK_ULONG  ulDataLen,
			CK_BYTE_PTR pSignature,
			CK_ULONG_PTR pulSignatureLen)
{
  CK_RV rv;
  enter("C_SignRecover");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pData[ulDataLen]", pData, ulDataLen);
  rv = po->C_SignRecover(hSession, pData, ulDataLen,
			 pSignature, pulSignatureLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pSignature[*pulSignatureLen]",
			pSignature, *pulSignatureLen);
  }
  return retne(rv);
}

CK_RV C_VerifyInit(CK_SESSION_HANDLE hSession,
		       CK_MECHANISM_PTR pMechanism,
		       CK_OBJECT_HANDLE hKey)
{
  CK_RV rv;
  enter("C_VerifyInit");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  spy_dump_ulong_in("hKey", hKey);
  rv = po->C_VerifyInit(hSession, pMechanism, hKey);
  return retne(rv);
}


CK_RV C_Verify(CK_SESSION_HANDLE hSession,
		   CK_BYTE_PTR pData,
		   CK_ULONG  ulDataLen,
		   CK_BYTE_PTR pSignature,
		   CK_ULONG  ulSignatureLen)
{
  CK_RV rv;
  enter("C_Verify");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pData[ulDataLen]", pData, ulDataLen);
  spy_dump_string_in("pSignature[ulSignatureLen]",
		     pSignature, ulSignatureLen);
  rv = po->C_Verify(hSession, pData, ulDataLen, pSignature, ulSignatureLen);
  return retne(rv);
}


CK_RV C_VerifyUpdate(CK_SESSION_HANDLE hSession,
			 CK_BYTE_PTR pPart,
			 CK_ULONG  ulPartLen)
{
  CK_RV rv;
  enter("C_VerifyUpdate");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pPart[ulPartLen]", pPart, ulPartLen);
  rv = po->C_VerifyUpdate(hSession, pPart, ulPartLen);
  return retne(rv);
}


CK_RV C_VerifyFinal(CK_SESSION_HANDLE hSession,
			CK_BYTE_PTR pSignature,
			CK_ULONG  ulSignatureLen)
{
  CK_RV rv;
  enter("C_VerifyFinal");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pSignature[ulSignatureLen]",
		     pSignature, ulSignatureLen);
  rv = po->C_VerifyFinal(hSession, pSignature, ulSignatureLen);
  return retne(rv);
}


CK_RV C_VerifyRecoverInit(CK_SESSION_HANDLE hSession,
			      CK_MECHANISM_PTR pMechanism,
			      CK_OBJECT_HANDLE hKey)
{
  CK_RV rv;
  enter("C_VerifyRecoverInit");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  spy_dump_ulong_in("hKey", hKey);
  rv = po->C_VerifyRecoverInit(hSession, pMechanism, hKey);
  return retne(rv);
}


CK_RV C_VerifyRecover(CK_SESSION_HANDLE hSession,
			  CK_BYTE_PTR pSignature,
			  CK_ULONG  ulSignatureLen,
			  CK_BYTE_PTR pData,
			  CK_ULONG_PTR pulDataLen)
{
  CK_RV rv;
  enter("C_VerifyRecover");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pSignature[ulSignatureLen]",
		     pSignature, ulSignatureLen);
  rv = po->C_VerifyRecover(hSession, pSignature, ulSignatureLen,
			   pData, pulDataLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pData[*pulDataLen]", pData, *pulDataLen);
  }
  return retne(rv);
}

CK_RV C_DigestEncryptUpdate(CK_SESSION_HANDLE hSession,
				CK_BYTE_PTR pPart,
				CK_ULONG  ulPartLen,
				CK_BYTE_PTR pEncryptedPart,
				CK_ULONG_PTR pulEncryptedPartLen)
{
  CK_RV rv;
  enter("C_DigestEncryptUpdate");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pPart[ulPartLen]", pPart, ulPartLen);
  rv = po->C_DigestEncryptUpdate(hSession, pPart, ulPartLen,
				 pEncryptedPart, pulEncryptedPartLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pEncryptedPart[*pulEncryptedPartLen]",
			pEncryptedPart, *pulEncryptedPartLen);
  }
  return retne(rv);
}


CK_RV C_DecryptDigestUpdate(CK_SESSION_HANDLE hSession,
				CK_BYTE_PTR pEncryptedPart,
				CK_ULONG  ulEncryptedPartLen,
				CK_BYTE_PTR pPart,
				CK_ULONG_PTR pulPartLen)
{
  CK_RV rv;
  enter("C_DecryptDigestUpdate");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pEncryptedPart[ulEncryptedPartLen]",
		      pEncryptedPart, ulEncryptedPartLen);
  rv = po->C_DecryptDigestUpdate(hSession, pEncryptedPart,
				 ulEncryptedPartLen,
				 pPart,  pulPartLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pPart[*pulPartLen]", pPart, *pulPartLen);
  }
  return retne(rv);
}


CK_RV C_SignEncryptUpdate(CK_SESSION_HANDLE hSession,
			      CK_BYTE_PTR pPart,
			      CK_ULONG  ulPartLen,
			      CK_BYTE_PTR pEncryptedPart,
			      CK_ULONG_PTR pulEncryptedPartLen)
{
  CK_RV rv;
  enter("C_SignEncryptUpdate");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pPart[ulPartLen]", pPart, ulPartLen);
  rv = po->C_SignEncryptUpdate(hSession, pPart, ulPartLen,
			       pEncryptedPart, pulEncryptedPartLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pEncryptedPart[*pulEncryptedPartLen]",
			pEncryptedPart, *pulEncryptedPartLen);
  }
  return retne(rv);
}


CK_RV C_DecryptVerifyUpdate(CK_SESSION_HANDLE hSession,
				CK_BYTE_PTR pEncryptedPart,
				CK_ULONG  ulEncryptedPartLen,
				CK_BYTE_PTR pPart,
				CK_ULONG_PTR pulPartLen)
{
  CK_RV rv;
  enter("C_DecryptVerifyUpdate");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pEncryptedPart[ulEncryptedPartLen]",
		      pEncryptedPart, ulEncryptedPartLen);
  rv = po->C_DecryptVerifyUpdate(hSession, pEncryptedPart,
				 ulEncryptedPartLen, pPart,
				 pulPartLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pPart[*pulPartLen]", pPart, *pulPartLen);
  }
  return retne(rv);
}

CK_RV C_GenerateKey(CK_SESSION_HANDLE hSession,
			CK_MECHANISM_PTR pMechanism,
			CK_ATTRIBUTE_PTR pTemplate,
			CK_ULONG  ulCount,
			CK_OBJECT_HANDLE_PTR phKey)
{
  CK_RV rv;
  enter("C_GenerateKey");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  spy_attribute_list_in("pTemplate", pTemplate, ulCount);
  rv = po->C_GenerateKey(hSession, pMechanism, pTemplate,
			 ulCount, phKey);
  if (rv == CKR_OK) {
    spy_dump_ulong_out("hKey", *phKey);
  }
  return retne(rv);
}

CK_RV C_GenerateKeyPair(CK_SESSION_HANDLE hSession,
			    CK_MECHANISM_PTR pMechanism,
			    CK_ATTRIBUTE_PTR pPublicKeyTemplate,
			    CK_ULONG  ulPublicKeyAttributeCount,
			    CK_ATTRIBUTE_PTR pPrivateKeyTemplate,
			    CK_ULONG  ulPrivateKeyAttributeCount,
			    CK_OBJECT_HANDLE_PTR phPublicKey,
			    CK_OBJECT_HANDLE_PTR phPrivateKey)
{
  CK_RV rv;
  enter("C_GenerateKeyPair");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  spy_attribute_list_in("pPublicKeyTemplate",
			pPublicKeyTemplate, ulPublicKeyAttributeCount);
  spy_attribute_list_in("pPrivateKeyTemplate",
			pPrivateKeyTemplate, ulPrivateKeyAttributeCount);
  rv = po->C_GenerateKeyPair(hSession, pMechanism, pPublicKeyTemplate,
			     ulPublicKeyAttributeCount, pPrivateKeyTemplate,
			     ulPrivateKeyAttributeCount, phPublicKey,
			     phPrivateKey);
  if (rv == CKR_OK) {
    spy_dump_ulong_out("hPublicKey", *phPublicKey);
    spy_dump_ulong_out("hPrivateKey", *phPrivateKey);
  }
  return retne(rv);
}


CK_RV C_WrapKey(CK_SESSION_HANDLE hSession,
		    CK_MECHANISM_PTR pMechanism,
		    CK_OBJECT_HANDLE hWrappingKey,
		    CK_OBJECT_HANDLE hKey,
		    CK_BYTE_PTR pWrappedKey,
		    CK_ULONG_PTR pulWrappedKeyLen)
{
  CK_RV rv;
  enter("C_WrapKey");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  spy_dump_ulong_in("hWrappingKey", hWrappingKey);
  spy_dump_ulong_in("hKey", hKey);
  rv = po->C_WrapKey(hSession, pMechanism, hWrappingKey,
		     hKey, pWrappedKey, pulWrappedKeyLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("pWrappedKey[*pulWrappedKeyLen]",
			pWrappedKey, *pulWrappedKeyLen);
  }
  return retne(rv);
}

CK_RV C_UnwrapKey(CK_SESSION_HANDLE hSession,
		      CK_MECHANISM_PTR pMechanism,
		      CK_OBJECT_HANDLE hUnwrappingKey,
		      CK_BYTE_PTR  pWrappedKey,
		      CK_ULONG  ulWrappedKeyLen,
		      CK_ATTRIBUTE_PTR pTemplate,
		      CK_ULONG  ulAttributeCount,
		      CK_OBJECT_HANDLE_PTR phKey)
{
  CK_RV rv;
  enter("C_UnwrapKey");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  spy_dump_ulong_in("hUnwrappingKey", hUnwrappingKey);
  spy_dump_string_in("pWrappedKey[ulWrappedKeyLen]",
		      pWrappedKey, ulWrappedKeyLen);
  spy_attribute_list_in("pTemplate", pTemplate, ulAttributeCount);
  rv = po->C_UnwrapKey(hSession, pMechanism, hUnwrappingKey,
		       pWrappedKey, ulWrappedKeyLen, pTemplate,
		       ulAttributeCount, phKey);
  if (rv == CKR_OK) {
    spy_dump_ulong_out("hKey", *phKey);
  }
  return retne(rv);
}

CK_RV C_DeriveKey(CK_SESSION_HANDLE hSession,
		      CK_MECHANISM_PTR pMechanism,
		      CK_OBJECT_HANDLE hBaseKey,
		      CK_ATTRIBUTE_PTR pTemplate,
		      CK_ULONG  ulAttributeCount,
		      CK_OBJECT_HANDLE_PTR phKey)
{
  CK_RV rv;
  enter("C_DeriveKey");
  spy_dump_ulong_in("hSession", hSession);
  fprintf(spy_output, "pMechanism->type=%s\n",
	  lookup_enum(MEC_T, pMechanism->mechanism));
  spy_dump_ulong_in("hBaseKey", hBaseKey);
  spy_attribute_list_in("pTemplate", pTemplate, ulAttributeCount);
  rv = po->C_DeriveKey(hSession, pMechanism, hBaseKey,
		       pTemplate, ulAttributeCount, phKey);
  if (rv == CKR_OK) {
    spy_dump_ulong_out("hKey", *phKey);
  }
  return retne(rv);
}

CK_RV C_SeedRandom(CK_SESSION_HANDLE hSession,
		       CK_BYTE_PTR pSeed,
		       CK_ULONG  ulSeedLen)
{
  CK_RV rv;
  enter("C_SeedRandom");
  spy_dump_ulong_in("hSession", hSession);
  spy_dump_string_in("pSeed[ulSeedLen]", pSeed, ulSeedLen);
  rv = po->C_SeedRandom(hSession, pSeed, ulSeedLen);
  return retne(rv);
}


CK_RV C_GenerateRandom(CK_SESSION_HANDLE hSession,
			   CK_BYTE_PTR RandomData,
			   CK_ULONG  ulRandomLen)
{
  CK_RV rv;
  enter("C_GenerateRandom");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_GenerateRandom(hSession, RandomData, ulRandomLen);
  if (rv == CKR_OK) {
    spy_dump_string_out("RandomData[ulRandomLen]",
			RandomData, ulRandomLen);
  }
  return retne(rv);
}


CK_RV C_GetFunctionStatus(CK_SESSION_HANDLE hSession)
{
  CK_RV rv;
  enter("C_GetFunctionStatus");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_GetFunctionStatus(hSession);
  return retne(rv);
}

CK_RV C_CancelFunction(CK_SESSION_HANDLE hSession)
{
  CK_RV rv;
  enter("C_CancelFunction");
  spy_dump_ulong_in("hSession", hSession);
  rv = po->C_CancelFunction(hSession);
  return retne(rv);
}

CK_RV C_WaitForSlotEvent(CK_FLAGS flags,
			     CK_SLOT_ID_PTR pSlot,
			     CK_VOID_PTR pRserved)
{
  CK_RV rv;
  enter("C_WaitForSlotEvent");
  rv = po->C_WaitForSlotEvent(flags, pSlot, pRserved);
  return retne(rv);
}
