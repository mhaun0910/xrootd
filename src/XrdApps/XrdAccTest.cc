/******************************************************************************/
/*                                                                            */
/*                         X r d A c c T e s t . c c                          */
/*                                                                            */
/* (c) 2017 by the Board of Trustees of the Leland Stanford, Jr., University  */
/*                            All Rights Reserved                             */
/*   Produced by Andrew Hanushevsky for Stanford University under contract    */
/*              DE-AC02-76-SFO0515 with the Department of Energy              */
/*                                                                            */
/* This file is part of the XRootD software suite.                            */
/*                                                                            */
/* XRootD is free software: you can redistribute it and/or modify it under    */
/* the terms of the GNU Lesser General Public License as published by the     */
/* Free Software Foundation, either version 3 of the License, or (at your     */
/* option) any later version.                                                 */
/*                                                                            */
/* XRootD is distributed in the hope that it will be useful, but WITHOUT      */
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public       */
/* License for more details.                                                  */
/*                                                                            */
/* You should have received a copy of the GNU Lesser General Public License   */
/* along with XRootD in a file called COPYING.LESSER (LGPL license) and file  */
/* COPYING (GPL license).  If not, see <http://www.gnu.org/licenses/>.        */
/*                                                                            */
/* The copyright holder's institutional names and contributor's names may not */
/* be used to endorse or promote products derived from this software without  */
/* specific prior written permission of the institution or contributor.       */
/******************************************************************************/
  
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <strings.h>
#include <stdio.h>
#include <grp.h>
#include <arpa/inet.h>
#include <sys/param.h>
#include <sys/socket.h>

#include "XrdVersion.hh"

#include "XrdAcc/XrdAccAuthorize.hh"
#include "XrdAcc/XrdAccConfig.hh"
#include "XrdAcc/XrdAccGroups.hh"
#include "XrdAcc/XrdAccPrivs.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdSys/XrdSysHeaders.hh"
#include "XrdSys/XrdSysLogger.hh"
#include "XrdNet/XrdNetAddr.hh"
#include "XrdOuc/XrdOucEnv.hh"
#include "XrdOuc/XrdOucStream.hh"

/******************************************************************************/
/*                        G l o b a l   O b j e c t s                         */
/******************************************************************************/

char *PrivsConvert(XrdAccPrivCaps &ctab, char *buff, int blen);

XrdAccAuthorize *Authorize;

int  extra;

XrdSysLogger myLogger;

XrdSysError  eroute(&myLogger, "acc_");

namespace
{
XrdSecEntity Entity("host");

XrdNetAddr   netAddr;

bool v2 = false;
}

/******************************************************************************/
/*                       O p e r a t i o n   T a b l e                        */
/******************************************************************************/
typedef struct {const char *opname; Access_Operation oper;} optab_t;
optab_t optab[] =
             {{"?",      AOP_Any},
              {"cm",     AOP_Chmod},
              {"co",     AOP_Chown},
              {"cr",     AOP_Create},
              {"rm",     AOP_Delete},
              {"lk",     AOP_Lock},
              {"mk",     AOP_Mkdir},
              {"mv",     AOP_Rename},
              {"rd",     AOP_Read},
              {"ls",     AOP_Readdir},
              {"st",     AOP_Stat},
              {"wr",     AOP_Update}
             };

int opcnt = sizeof(optab)/sizeof(optab[0]);

/******************************************************************************/
/*                                 U s a g e                                  */
/******************************************************************************/

void Usage(const char *msg)
{
   if (msg) cerr <<"xrdacctest: " <<msg <<endl;
   cerr <<"Usage: xrdacctest [-c <cfn>] [<ids> | <user> <host>] <act>\n\n";
   cerr <<"<ids>: -a <auth> -g <grp> -h <host> -o <org> -r <role> -u <user>\n";
   cerr <<"<act>: <opc> <path> [<path> [...]]\n";
   cerr <<"<act>: <opc> <path> [<path> [...]]\n";
   cerr <<"<opc>: cr - create    mv - rename    st - status    lk - lock\n";
   cerr <<"       rd - read      wr - write     ls - readdir   rm - remove\n";
   cerr <<"       *  - zap args  ?  - display privs\n";
   cerr <<flush;
   exit(msg ? 1 : 0);
}
  
/******************************************************************************/
/*                             Z a p E n t i t y                              */
/******************************************************************************/

void ZapEntity()
{
   strncpy(Entity.prot, "host", sizeof(Entity.prot));
   Entity.grps = 0;
   Entity.host = 0;
   Entity.vorg = 0;
   Entity.role = 0;
   Entity.name = 0;
}
  
/******************************************************************************/
/*                                  m a i n                                   */
/******************************************************************************/
  
int main(int argc, char **argv)
{
static XrdVERSIONINFODEF(myVer, XrdAccTest, XrdVNUMBER, XrdVERSION);
extern int   optind;
extern char *optarg;
extern XrdAccAuthorize *XrdAccDefaultAuthorizeObject(XrdSysLogger   *lp,
                                                     const char     *cfn,
                                                     const char     *parm,
                                                     XrdVersionInfo &myVer);
int DoIt(int argpnt, int argc, char **argv, bool singleshot);

const char *cfHost = "localhost", *cfProg = "xrootd";
char *p2l(XrdAccPrivs priv, char *buff, int blen);
char *argval[32], buff[255], c;
int DoIt(int argnum, int argc, char **argv, int singleshot);
XrdOucStream Command;
const int maxargs = sizeof(argval)/sizeof(argval[0]);
char *at, *ConfigFN = (char *)"./acc.cf";
int argnum, rc = 0;
bool singleshot=false;

// Print help if no args
//
  if (argc == 1) Usage(0);
  Entity.addrInfo = &netAddr;

// Get all of the options.
//
   while ((c=getopt(argc,argv,"a:c:dg:h:o:r:u:s")) != (char)EOF)
     { switch(c)
       {
       case 'a': strncpy(Entity.prot, optarg, sizeof(Entity.prot));
                                       v2 = true;    break;
       case 'd':                                     break;
       case 'g': Entity.grps = optarg; v2 = true;    break;
       case 'h': Entity.host = optarg; v2 = true;    break;
       case 'o': Entity.vorg = optarg; v2 = true;    break;
       case 'r': Entity.role = optarg; v2 = true;    break;
       case 'u': Entity.name = optarg; v2 = true;    break;
       case 'c': ConfigFN = optarg;                  break;
       case 's': singleshot = true;                  break;
       default:  sprintf(buff, "-%c option is invalid.", c);
                 Usage(buff);
       }
     }

// Establish environment
//
   if ((at = index(ConfigFN, '@')))
      {*at++ = 0; if (*at) cfHost = at;}
   sprintf(buff, "%s anon@%s", cfProg, cfHost);
   XrdOucEnv::Export("XRDINSTANCE", buff);

// Obtain the authorization object
//
if (!(Authorize = XrdAccDefaultAuthorizeObject(&myLogger, ConfigFN, 0, myVer)))
   {cerr << "testaccess: Initialization failed." <<endl;
    exit(2);
   }

// If command line options specified, process this
//
   if (optind < argc) {rc = DoIt(optind, argc, argv, singleshot); exit(rc);}

// Start accepting command from standard in until eof
//
   Command.Attach(0);
   cerr << "Enter arguments: ";
   while(Command.GetLine())
       while((argval[1] = Command.GetToken()))
            {for (argnum=2;
                  argnum < maxargs && (argval[argnum]=Command.GetToken());
                  argnum++) {}
             rc |= DoIt(1, argnum, argval, singleshot=0);
             cerr << "Enter arguments: ";
            }

// All done
//
   exit(rc);
}

int DoIt(int argpnt, int argc, char **argv, bool singleshot)
{
char *opc, *opv, *path, *result, buff[80];
Access_Operation cmd2op(char *opname);
void Usage(const char *);
Access_Operation optype;
XrdAccPrivCaps pargs;
XrdAccPrivs auth;

// Get options (this may be interactive mode)
//
   while(argpnt < argc && *argv[argpnt] == '-')
        {opc = argv[argpnt++];
         if (argpnt >= argc)
            {sprintf(buff, "%s option value not specified.", opc);
             Usage(buff);
            }
          opv = argv[argpnt++];
         if (strlen(opc) != 2)
            {sprintf(buff, "%s option is invalid.", opc);
             Usage(buff);
            }
         switch(*(opc+1))
               {case 'a': strncpy(Entity.prot, opv, sizeof(Entity.prot));
                          v2 = true; break;
                case 'g': Entity.grps = opv; v2 = true; break;
                case 'h': Entity.host = opv; v2 = true; break;
                case 'o': Entity.vorg = opv; v2 = true; break;
                case 'r': Entity.role = opv; v2 = true; break;
                case 'u': Entity.name = opv; v2 = true; break;
                default:  sprintf(buff, "%s option is invalid.", opc);
                          Usage(buff);
                          break;
               }
        }

// Make sure user and host specified if v1 version being used
//
   if (!v2)
      {if (argpnt >= argc) Usage("user not specified.");
       Entity.name = argv[argpnt++];
       if (argpnt >= argc) Usage("host not specified.");
       Entity.host = argv[argpnt++];
      }

// Make sure op   specified
//
   if (argpnt >= argc) Usage("operation not specified.");
   if (!strcmp(argv[argpnt], "*"))
      {ZapEntity();
       return 0;
      }
   optype = cmd2op(argv[argpnt++]);

// Make sure path specified
//
   if (argpnt >= argc) Usage("path not specified.");

// Remove unwanted items
//
  if (Entity.grps && !strcmp(Entity.grps, "none")) Entity.grps = 0;
  if (Entity.host && !strcmp(Entity.host, "none")) Entity.host = 0;
  if (Entity.vorg && !strcmp(Entity.vorg, "none")) Entity.vorg = 0;
  if (Entity.role && !strcmp(Entity.role, "none")) Entity.role = 0;
  if (Entity.name && !strcmp(Entity.name, "none")) Entity.name = 0;

// Set host, ignore errors
//
  if (Entity.host) netAddr.Set(Entity.host, 0);

// Process each path, as needed
//                                                            x
   while(argpnt < argc)
        {path = argv[argpnt++];
         auth = Authorize->Access((const XrdSecEntity *)&Entity,
                                  (const char *)path,
                                                optype);
         if (optype != AOP_Any) result=(auth?(char *)"allowed":(char *)"denied");
            else {pargs.pprivs = auth; pargs.nprivs = XrdAccPriv_None;
                  result = PrivsConvert(pargs, buff, sizeof(buff));
                 }
         cout <<result <<": " <<path <<endl;
         if (singleshot) return !auth;
       }

return 0;
}

/******************************************************************************/
/*                                c m d 2 o p                                 */
/******************************************************************************/
  
Access_Operation cmd2op(char *opname)
{
   int i;
   for (i = 0; i < opcnt; i++) 
       if (!strcmp(opname, optab[i].opname)) return optab[i].oper;
   cerr << "testaccess: Invalid operation - " <<opname <<endl;
   return AOP_Any;
}

/******************************************************************************/
/*                          P r i v s C o n v e r t                           */
/******************************************************************************/
  
char *PrivsConvert(XrdAccPrivCaps &ctab, char *buff, int blen)
{
     int i=0, j, k=2, bmax = blen-1;
     XrdAccPrivs privs;
     static struct {XrdAccPrivs pcode; char plet;} p2l[] =
                   {{XrdAccPriv_Delete,  'd'},
                    {XrdAccPriv_Insert,  'i'},
                    {XrdAccPriv_Lock,    'k'},
                    {XrdAccPriv_Lookup,  'l'},
                    {XrdAccPriv_Rename,  'n'},
                    {XrdAccPriv_Read,    'r'},
                    {XrdAccPriv_Write,   'w'}
                   };
     static int p2lnum = sizeof(p2l)/sizeof(p2l[0]);

     privs = ctab.pprivs;
     while(k--)
       {for (j = 0; j < p2lnum && i < bmax; j++)
            if (privs & p2l[j].pcode) buff[i++] = p2l[j].plet;
        if (i < bmax && ctab.nprivs != XrdAccPriv_None) buff[i++] = '-';
           else break;
        privs = ctab.nprivs;
       }
     buff[i] = '\0';
     return buff;
}