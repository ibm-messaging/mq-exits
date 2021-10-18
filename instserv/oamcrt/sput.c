/* %Z% %W% %I% %E% %U% */
 /********************************************************************/
 /*                                                                  */
 /* Program name: PUT0                                               */
 /*                                                                  */
 /* Description: Sample C program that puts messages to              */
 /*              a message queue (example using MQPUT)               */
 /*                                                                  */
 /*  SLIGHTLY MODIFIED FROM AMQSPUT0.C SAMPLE TO MAKE IT EASIER      */
 /*   TO SPECIFY "DEFAULT" OPTIONS                                   */
 /*                                                                  */
 /*   <copyright                                                     */
 /*   notice="lm-source-program"                                     */
 /*   pids="5724-H72"                                                */
 /*   years="1994,2021"                                              */
 /*   crc="2248028677" >                                             */
 /*   Licensed Materials - Property of IBM                           */
 /*                                                                  */
 /*   5724-H72                                                       */
 /*                                                                  */
 /*   (C) Copyright IBM Corp. 1994, 2021 All Rights Reserved.        */
 /*                                                                  */
 /*   US Government Users Restricted Rights - Use, duplication or    */
 /*   disclosure restricted by GSA ADP Schedule Contract with        */
 /*   IBM Corp.                                                      */
 /*   </copyright>                                                   */
 /********************************************************************/
 /*                                                                  */
 /* Function:                                                        */
 /*                                                                  */
 /*                                                                  */
 /*   AMQSPUT0 is a sample C program to put messages on a message    */
 /*   queue, and is an example of the use of MQPUT.                  */
 /*                                                                  */
 /*      -- messages are sent to the queue named by the parameter    */
 /*                                                                  */
 /*      -- gets lines from StdIn, and adds each to target           */
 /*         queue, taking each line of text as the content           */
 /*         of a datagram message; the sample stops when a null      */
 /*         line (or EOF) is read.                                   */
 /*         New-line characters are removed.                         */
 /*         If a line is longer than 65534 characters it is broken   */
 /*         up into 65534-character pieces. Each piece becomes the   */
 /*         content of a datagram message.                           */
 /*         If the length of a line is a multiple of 65534 plus 1    */
 /*         e.g. 131069, the last piece will only contain a new-line */
 /*         character so will terminate the input.                   */
 /*                                                                  */
 /*      -- writes a message for each MQI reason other than          */
 /*         MQRC_NONE; stops if there is a MQI completion code       */
 /*         of MQCC_FAILED                                           */
 /*                                                                  */
 /*    Program logic:                                                */
 /*         MQOPEN target queue for OUTPUT                           */
 /*         while end of input file not reached,                     */
 /*         .  read next line of text                                */
 /*         .  MQPUT datagram message with text line as data         */
 /*         MQCLOSE target queue                                     */
 /*                                                                  */
 /*                                                                  */
 /********************************************************************/
 /*                                                                  */
 /*   AMQSPUT0 has the following parameters                          */
 /*       required:                                                  */
 /*                 (1) The name of the target queue                 */
 /*       optional:                                                  */
 /*                 (2) Queue manager name                           */
 /*                 (3) The open options                             */
 /*                 (4) The close options                            */
 /*                 (5) The name of the target queue manager         */
 /*                 (6) The name of the dynamic queue                */
 /*                                                                  */
 /*  Environment variable MQSAMP_USER_ID can be set to authenticate  */
 /*  application. If it is set, a password must also be entered at   */
 /*  the prompt.                                                     */
 /*  MODIFICATIONS: (3) and (4) can be given as '-1' to take         */
 /*                 default values.                                  */
 /*                 (5) can be given as "_" to take default value    */
 /*                                                                  */
 /********************************************************************/
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
    /* includes for MQI */
 #include <cmqc.h>

    /* Platform includes for masked input */
#if (MQAT_DEFAULT == MQAT_OS400)
  #include <qp0ztrml.h>
#elif (MQAT_DEFAULT == MQAT_WINDOWS_NT)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <io.h>
#elif (MQAT_DEFAULT == MQAT_UNIX)
  #include <termios.h>
  #include <unistd.h>
#endif
 void get_password(char *buffer, size_t size);

 int main(int argc, char **argv)
 {
   /*  Declare file and character for sample input                   */
   FILE *fp;

   /*   Declare MQI structures needed                                */
   MQOD     od = {MQOD_DEFAULT};    /* Object Descriptor             */
   MQMD     md = {MQMD_DEFAULT};    /* Message Descriptor            */
   MQPMO   pmo = {MQPMO_DEFAULT};   /* put message options           */
   MQCNO   cno = {MQCNO_DEFAULT};   /* connection options            */
   MQCSP   csp = {MQCSP_DEFAULT};   /* security parameters           */
      /** note, sample uses defaults where it can **/

   MQHCONN  Hcon;                   /* connection handle             */
   MQHOBJ   Hobj;                   /* object handle                 */
   MQLONG   O_options;              /* MQOPEN options                */
   MQLONG   C_options;              /* MQCLOSE options               */
   MQLONG   CompCode;               /* completion code               */
   MQLONG   OpenCode;               /* MQOPEN completion code        */
   MQLONG   Reason;                 /* reason code                   */
   MQLONG   CReason;                /* reason code for MQCONNX       */
   MQLONG   messlen;                /* message length                */
   char     buffer[65535];          /* message buffer                */
   char     QMName[50];             /* queue manager name            */
   char    *UserId;                 /* UserId for authentication     */
   char     Password[MQ_CSP_PASSWORD_LENGTH + 1] = {0}; /* For auth  */

   printf("Sample AMQSPUT0 start\n");
   if (argc < 2)
   {
     printf("Required parameter missing - queue name\n");
     exit(99);
   }

   /******************************************************************/
   /* Setup any authentication information supplied in the local     */
   /* environment. The connection options structure points to the    */
   /* security structure. If the userid is set, then the password    */
   /* is read from the terminal. Having the password entered this    */
   /* way avoids it being accessible from other programs that can    */
   /* inspect command line parameters or environment variables.      */
   /******************************************************************/
   UserId = getenv("MQSAMP_USER_ID");
   if (UserId != NULL)
   {
     /****************************************************************/
     /* Set the connection options to use the security structure and */
     /* set version information to ensure the structure is processed.*/
     /****************************************************************/
     cno.SecurityParmsPtr = &csp;
     cno.Version = MQCNO_VERSION_5;

     csp.AuthenticationType = MQCSP_AUTH_USER_ID_AND_PWD;
     csp.CSPUserIdPtr = UserId;
     csp.CSPUserIdLength = (MQLONG)strlen(UserId);

     /****************************************************************/
     /* Get the password, using masked input if possible             */
     /****************************************************************/
     printf("Enter password: ");
     get_password(Password,sizeof(Password)-1);

     if (strlen(Password) > 0 && Password[strlen(Password) - 1] == '\n')
       Password[strlen(Password) -1] = 0;
     csp.CSPPasswordPtr = Password;
     csp.CSPPasswordLength = (MQLONG)strlen(csp.CSPPasswordPtr);
   }

   /******************************************************************/
   /*                                                                */
   /*   Connect to queue manager                                     */
   /*                                                                */
   /******************************************************************/
   QMName[0] = 0;    /* default */
   if (argc > 2)
     strncpy(QMName, argv[2], (size_t)MQ_Q_MGR_NAME_LENGTH);


   MQCONNX(QMName,                 /* queue manager                  */
          &cno,                    /* connection options             */
          &Hcon,                   /* connection handle              */
          &CompCode,               /* completion code                */
          &CReason);               /* reason code                    */

   /* report reason and stop if it failed     */
   if (CompCode == MQCC_FAILED)
   {
     printf("MQCONNX ended with reason code %d\n", CReason);
     exit( (int)CReason );
   }

   /* if there was a warning report the cause and continue */
   if (CompCode == MQCC_WARNING)
   {
     printf("MQCONNX generated a warning with reason code %d\n", CReason);
     printf("Continuing...\n");
   }
   /******************************************************************/
   /*                                                                */
   /*   Use parameter as the name of the target queue                */
   /*                                                                */
   /******************************************************************/
   strncpy(od.ObjectName, argv[1], (size_t)MQ_Q_NAME_LENGTH);
   printf("target queue is %s\n", od.ObjectName);

   if (argc > 5 && strcmp(argv[5],"_") != 0)
   {
     strncpy(od.ObjectQMgrName, argv[5], (size_t) MQ_Q_MGR_NAME_LENGTH);
     printf("target queue manager is %s\n", od.ObjectQMgrName);
   }

   if (argc > 6 && strcmp(argv[6],"_")!=0)
   {
     strncpy(od.DynamicQName, argv[6], (size_t) MQ_Q_NAME_LENGTH);
     printf("dynamic queue name is %s\n", od.DynamicQName);
   }

   /******************************************************************/
   /*                                                                */
   /*   Open the target message queue for output                     */
   /*                                                                */
   /******************************************************************/
   if (argc > 3 && atoi(argv[3]) != -1)
   {
     O_options = atoi( argv[3] );
     printf("open  options are %d\n", O_options);
   }
   else
   {
     O_options = MQOO_OUTPUT            /* open queue for output     */
               | MQOO_FAIL_IF_QUIESCING /* but not if MQM stopping   */
               ;                        /* = 0x2010 = 8208 decimal   */
   }

   MQOPEN(Hcon,                      /* connection handle            */
          &od,                       /* object descriptor for queue  */
          O_options,                 /* open options                 */
          &Hobj,                     /* object handle                */
          &OpenCode,                 /* MQOPEN completion code       */
          &Reason);                  /* reason code                  */

   /* report reason, if any; stop if failed      */
   if (Reason != MQRC_NONE)
   {
     printf("MQOPEN ended with reason code %d\n", Reason);
   }

   if (OpenCode == MQCC_FAILED)
   {
     printf("unable to open queue for output\n");
   }

   /******************************************************************/
   /*                                                                */
   /*   Read lines from the file and put them to the message queue   */
   /*   Loop until null line or end of file, or there is a failure   */
   /*                                                                */
   /******************************************************************/
   CompCode = OpenCode;        /* use MQOPEN result for initial test */
   fp = stdin;

   memcpy(md.Format,           /* character string format            */
          MQFMT_STRING, (size_t)MQ_FORMAT_LENGTH);

   pmo.Options = MQPMO_NO_SYNCPOINT
               | MQPMO_FAIL_IF_QUIESCING;

   /******************************************************************/
   /* Use these options when connecting to Queue Managers that also  */
   /* support them, see the Application Programming Reference for    */
   /* details.                                                       */
   /* The MQPMO_NEW_MSG_ID option causes the MsgId to be replaced,   */
   /* so that there is no need to reset it before each MQPUT.        */
   /* The MQPMO_NEW_CORREL_ID option causes the CorrelId to be       */
   /* replaced.                                                      */
   /******************************************************************/
   /* pmo.Options |= MQPMO_NEW_MSG_ID;                               */
   /* pmo.Options |= MQPMO_NEW_CORREL_ID;                            */

   while (CompCode != MQCC_FAILED)
   {
     if (fgets(buffer, sizeof(buffer), fp) != NULL)
     {
       messlen = (MQLONG)strlen(buffer); /* length without null      */
       if (buffer[messlen-1] == '\n')  /* last char is a new-line    */
       {
         buffer[messlen-1]  = '\0';    /* replace new-line with null */
         --messlen;                    /* reduce buffer length       */
       }
     }
     else messlen = 0;        /* treat EOF same as null line         */

     /****************************************************************/
     /*                                                              */
     /*   Put each buffer to the message queue                       */
     /*                                                              */
     /****************************************************************/
     if (messlen > 0)
     {
       /**************************************************************/
       /* The following statement is not required if the             */
       /* MQPMO_NEW_MSG_ID option is used.                           */
       /**************************************************************/
       memcpy(md.MsgId,           /* reset MsgId to get a new one    */
              MQMI_NONE, sizeof(md.MsgId) );

       MQPUT(Hcon,                /* connection handle               */
             Hobj,                /* object handle                   */
             &md,                 /* message descriptor              */
             &pmo,                /* default options (datagram)      */
             messlen,             /* message length                  */
             buffer,              /* message buffer                  */
             &CompCode,           /* completion code                 */
             &Reason);            /* reason code                     */

       /* report reason, if any */
       if (Reason != MQRC_NONE)
       {
         printf("MQPUT ended with reason code %d\n", Reason);
       }
     }
     else   /* satisfy end condition when empty line is read */
       CompCode = MQCC_FAILED;
   }

   /******************************************************************/
   /*                                                                */
   /*   Close the target queue (if it was opened)                    */
   /*                                                                */
   /******************************************************************/
   if (OpenCode != MQCC_FAILED)
   {
     if (argc > 4 && atoi(argv[4]) != -1)
     {
       C_options = atoi( argv[4] );
       printf("close options are %d\n", C_options);
     }
     else
     {
       C_options = MQCO_NONE;        /* no close options             */
     }

     MQCLOSE(Hcon,                   /* connection handle            */
             &Hobj,                  /* object handle                */
             C_options,
             &CompCode,              /* completion code              */
             &Reason);               /* reason code                  */

     /* report reason, if any     */
     if (Reason != MQRC_NONE)
     {
       printf("MQCLOSE ended with reason code %d\n", Reason);
     }
   }

   /******************************************************************/
   /*                                                                */
   /*   Disconnect from MQM if not already connected                 */
   /*                                                                */
   /******************************************************************/
   if (CReason != MQRC_ALREADY_CONNECTED)
   {
     MQDISC(&Hcon,                   /* connection handle            */
            &CompCode,               /* completion code              */
            &Reason);                /* reason code                  */

     /* report reason, if any     */
     if (Reason != MQRC_NONE)
     {
       printf("MQDISC ended with reason code %d\n", Reason);
     }
   }

   /******************************************************************/
   /*                                                                */
   /* END OF AMQSPUT0                                                */
   /*                                                                */
   /******************************************************************/
   printf("Sample AMQSPUT0 end\n");
   return(0);
 }

 /********************************************************************/
 /* Function name:    get_password                                   */
 /*                                                                  */
 /* Description:      Gets a password string from stdin, if possible */
 /*                   using masked input.                            */
 /*                                                                  */
 /* Called by:        main                                           */
 /*                                                                  */
 /* Receives:         buffer and size                                */
 /*                                                                  */
 /* Calls:            platform specific functions / fgets            */
 /*                                                                  */
 /********************************************************************/
#if (MQAT_DEFAULT == MQAT_OS400)
 void get_password(char *buffer, size_t size)
 {
   if (Qp0zIsATerminal(fileno(stdin)))
   {
     Qp0zSetTerminalMode( QP0Z_TERMINAL_INPUT_MODE, QP0Z_TERMINAL_HIDDEN, NULL );
     fgets(buffer, size, stdin);
     Qp0zSetTerminalMode( QP0Z_TERMINAL_INPUT_MODE, QP0Z_TERMINAL_PREVIOUS, NULL );
   }
   else
   {
     fgets(buffer, size, stdin);
   }
 }
#elif (MQAT_DEFAULT == MQAT_WINDOWS_NT)
 void get_password(char *buffer, size_t size)
 {
   int c;
   size_t i;
   HANDLE h;
   DWORD  readChars, oldMode, mode;
   BOOL b;
   char charBuf[1];

   h = GetStdHandle(STD_INPUT_HANDLE);
   if (_isatty(fileno(stdin)) && h != INVALID_HANDLE_VALUE)
   {
     GetConsoleMode(h, &mode);
     oldMode = mode;
     mode = (mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
     SetConsoleMode(h, mode);

     i=0;
     do
     {
       b = ReadConsole(h, charBuf, 1, &readChars, NULL);
       c = charBuf[0];
       if (b && readChars != 0 && c != '\n' && c != '\r')
       {
         if (c == '\b')
         {
           if (i > 0)
           {
             buffer[--i]=0;
             fprintf(stdout, "\b \b");
             fflush(stdout);
           }
         }
         else
         {
           fputc('*', stdout);
           fflush(stdout);
           buffer[i++] = c;
         }
       }
     } while (b && c != '\n' && c != '\r' && i <= size);
     printf("\n");
     SetConsoleMode(h, oldMode);
   }
   else
   {
     fgets(buffer, (int)size, stdin);
   }
 }
#elif (MQAT_DEFAULT == MQAT_UNIX)
 void get_password(char *buffer, size_t size)
 {
   int c;
   size_t i;
   struct termios savetty, newtty;
   const char BACKSPACE=8;
   const char DELETE=127;
   const char RETURN=10;
   int min = 1;
   int time = 0;

   if (isatty(fileno(stdin)))
   {
     tcgetattr(fileno(stdin), &savetty);
     newtty = savetty;
     newtty.c_cc[VMIN] = min;
     newtty.c_cc[VTIME] = time;
     newtty.c_lflag &= ~(ECHO|ICANON);
     tcsetattr(fileno(stdin), TCSANOW, &newtty);

     i=0;
     do
     {
       c = fgetc(stdin);
       if (c != EOF && c != RETURN)
       {
         if ( (c == BACKSPACE) || (c == DELETE) )
         {
           if (i > 0)
           {
             buffer[--i]=0;
             fprintf(stdout, "\b \b");
             fflush(stdout);
           }
         }
         else
         {
           fputc('*', stdout);
           fflush(stdout);
           buffer[i++] = c;
         }
       }
       else
       {
         buffer[i]=0;
       }
     } while (c != EOF && c != RETURN && i <= size);

     printf("\n");
     fflush(stdout);
     tcsetattr(fileno(stdin), TCSANOW, &savetty);
   }
   else
   {
     fgets(buffer, size, stdin);
   }
 }
#else
 void get_password(char *buffer, size_t size)
 {
   fgets(buffer, (int)size, stdin);
 }
#endif
