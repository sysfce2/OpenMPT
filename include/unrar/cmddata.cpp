#include "rar.hpp"

#include "cmdfilter.cpp"
#include "cmdmix.cpp"

CommandData::CommandData()
{
  Init();
}


void CommandData::Init()
{
  RAROptions::Init();

  Command.clear();
  ArcName.clear();
  ExtrPath.clear();
  TempPath.clear();
  SFXModule.clear();
  CommentFile.clear();
  ArcPath.clear();
  ExclArcPath.clear();
  LogName.clear();
  EmailTo.clear();
  UseStdin.clear();

  FileLists=false;
  NoMoreSwitches=false;

  ListMode=RCLM_AUTO;

  BareOutput=false;


  FileArgs.Reset();
  ExclArgs.Reset();
  InclArgs.Reset();
  ArcNames.Reset();
  StoreArgs.Reset();
#ifdef PROPAGATE_MOTW
  MotwList.Reset();
#endif
  Password.Clean();
  NextVolSizes.clear();
#ifdef RARDLL
  DllDestName.clear();
#endif
}


#if !defined(SFX_MODULE)
void CommandData::ParseCommandLine(bool Preprocess,int argc, char *argv[])
{
  Command.clear();
  NoMoreSwitches=false;
#ifdef CUSTOM_CMDLINE_PARSER
  // In Windows we may prefer to implement our own command line parser
  // to avoid replacing \" by " in standard parser. Such replacing corrupts
  // destination paths like "dest path\" in extraction commands.
  std::wstring CmdLine=GetCommandLine();

  std::wstring Param;
  std::wstring::size_type Pos=0;

  for (bool FirstParam=true;;FirstParam=false)
  {
    if (!GetCmdParam(CmdLine,Pos,Param))
      break;
    if (!FirstParam) // First parameter is the executable name.
      if (Preprocess)
        PreprocessArg(Param.c_str());
      else
        ParseArg(Param.c_str());
  }
#else
  for (int I=1;I<argc;I++)
  {
    std::wstring Arg;
    CharToWide(argv[I],Arg);
    if (Preprocess)
      PreprocessArg(Arg.c_str());
    else
      ParseArg(Arg.c_str());
  }
#endif
  if (!Preprocess)
    ParseDone();
}
#endif


#if !defined(SFX_MODULE)
void CommandData::ParseArg(const wchar *Arg)
{
  if (IsSwitch(*Arg) && !NoMoreSwitches)
    if (Arg[1]=='-' && Arg[2]==0)
      NoMoreSwitches=true;
    else
      ProcessSwitch(Arg+1);
  else
    if (Command.empty())
    {
      Command=Arg;


      Command[0]=toupperw(Command[0]);
      // 'I' and 'S' commands can contain case sensitive strings after
      // the first character, so we must not modify their case.
      // 'S' can contain SFX name, which case is important in Unix.
      if (Command[0]!='I' && Command[0]!='S')
        wcsupper(Command);
      if (Command[0]=='P') // Enforce -idq for print command.
      {
        MsgStream=MSG_ERRONLY;
        SetConsoleMsgStream(MSG_ERRONLY);
      }
    }
    else
      if (ArcName.empty())
        ArcName=Arg;
      else
      {
        // Check if last character is the path separator.
        size_t Length=wcslen(Arg);
        wchar EndChar=Length==0 ? 0:Arg[Length-1];
        // Check if trailing path separator like path\ is present.
        bool FolderArg=IsDriveDiv(EndChar) || IsPathDiv(EndChar);

        // 2024.01.05: We were asked to support exotic d:. and d:.. paths.
        if (IsDriveLetter(Arg) && Arg[2]=='.' && (Arg[3]==0 || Arg[3]=='.' && Arg[4]==0))
          FolderArg=true;

        // 2024.01.06: FindFile::FastFind check below fails in Windows 10 if
        // "." or ".." points at disk root. So we enforce it for "." and ".."
        // optionally preceded with some path like "..\..".
        size_t L=Length;
        if (L>0 && Arg[L-1]=='.' && (L==1 || L>=2 && (IsPathDiv(Arg[L-2]) ||
            Arg[L-2]=='.' && (L==2 || L>=3 && IsPathDiv(Arg[L-3])))))
          FolderArg=true;

        wchar CmdChar=toupperw(Command[0]);
        bool Add=wcschr(L"AFUM",CmdChar)!=NULL;
        bool Extract=CmdChar=='X' || CmdChar=='E';
        bool Repair=CmdChar=='R' && Command[1]==0;
        if (FolderArg && !Add)
          ExtrPath=Arg;
        else
          if ((Add || CmdChar=='T') && (*Arg!='@' || ListMode==RCLM_REJECT_LISTS))
            FileArgs.AddString(Arg);
          else
          {
            FindData FileData;
            bool Found=FindFile::FastFind(Arg,&FileData);
            if ((!Found || ListMode==RCLM_ACCEPT_LISTS) && 
                ListMode!=RCLM_REJECT_LISTS && *Arg=='@' && !IsWildcard(Arg+1))
            {
              FileLists=true;

              ReadTextFile(Arg+1,&FileArgs,false,true,FilelistCharset,true,true,true);

            }
            else // We use 'destpath\' when extracting and reparing.
              if (Found && FileData.IsDir && (Extract || Repair) && ExtrPath.empty())
              {
                ExtrPath=Arg;
                AddEndSlash(ExtrPath);
              }
              else
                FileArgs.AddString(Arg);
          }
      }
}
#endif


void CommandData::ParseDone()
{
  if (FileArgs.ItemsCount()==0 && !FileLists)
    FileArgs.AddString(MASKALL);
  wchar CmdChar=toupperw(Command[0]);
  bool Extract=CmdChar=='X' || CmdChar=='E' || CmdChar=='P';
  if (Test && Extract)
    Test=false;        // Switch '-t' is senseless for 'X', 'E', 'P' commands.

  // Suppress the copyright message and final end of line for 'lb' and 'vb'.
  if ((CmdChar=='L' || CmdChar=='V') && Command[1]=='B')
    BareOutput=true;
}


#if !defined(SFX_MODULE)
void CommandData::ParseEnvVar()
{
  char *EnvVar=getenv("RARINISWITCHES");
  if (EnvVar!=NULL)
  {
    std::wstring EnvStr;
    CharToWide(EnvVar,EnvStr);
    ProcessSwitchesString(EnvStr);
  }
}
#endif



#if !defined(SFX_MODULE)
// Preprocess those parameters, which must be processed before the rest of
// command line.
void CommandData::PreprocessArg(const wchar *Arg)
{
  if (IsSwitch(Arg[0]) && !NoMoreSwitches)
  {
    Arg++;
    if (Arg[0]=='-' && Arg[1]==0) // Switch "--".
      NoMoreSwitches=true;
    if (wcsicomp(Arg,L"cfg-")==0)
      ProcessSwitch(Arg);
    if (wcsnicomp(Arg,L"ilog",4)==0)
    {
      // Ensure that correct log file name is already set
      // if we need to report an error when processing the command line.
      ProcessSwitch(Arg);
      InitLogOptions(LogName,ErrlogCharset);
    }
    if (wcsnicomp(Arg,L"sc",2)==0)
    {
      // Process -sc before reading any file lists.
      ProcessSwitch(Arg);
      if (!LogName.empty())
        InitLogOptions(LogName,ErrlogCharset);
    }
  }
  else
    if (Command.empty())
      Command=Arg; // Need for rar.ini.
}
#endif


#if !defined(SFX_MODULE)
void CommandData::ReadConfig()
{
  StringList List;
  if (ReadTextFile(DefConfigName,&List,true))
  {
    wchar *Str;
    while ((Str=List.GetString())!=NULL)
    {
      while (IsSpace(*Str))
        Str++;
      if (wcsnicomp(Str,L"switches=",9)==0)
        ProcessSwitchesString(Str+9);
      if (!Command.empty())
      {
        wchar Cmd[16];
        wcsncpyz(Cmd,Command.c_str(),ASIZE(Cmd));
        wchar C0=toupperw(Cmd[0]);
        wchar C1=toupperw(Cmd[1]);
        if (C0=='I' || C0=='L' || C0=='M' || C0=='S' || C0=='V')
          Cmd[1]=0;
        if (C0=='R' && (C1=='R' || C1=='V'))
          Cmd[2]=0;
        wchar SwName[16+ASIZE(Cmd)];
        swprintf(SwName,ASIZE(SwName),L"switches_%ls=",Cmd);
        size_t Length=wcslen(SwName);
        if (wcsnicomp(Str,SwName,Length)==0)
          ProcessSwitchesString(Str+Length);
      }
    }
  }
}
#endif


#if !defined(SFX_MODULE)
void CommandData::ProcessSwitchesString(const std::wstring &Str)
{
  std::wstring Par;
  std::wstring::size_type Pos=0;
  while (GetCmdParam(Str,Pos,Par))
  {
    if (IsSwitch(Par[0]))
      ProcessSwitch(&Par[1]);
    else
    {
      mprintf(St(MSwSyntaxError),Par.c_str());
      ErrHandler.Exit(RARX_USERERROR);
    }
  }
}
#endif


#if !defined(SFX_MODULE)
void CommandData::ProcessSwitch(const wchar *Switch)
{

  if (LargePageAlloc::ProcessSwitch(this,Switch))
    return;

  switch(toupperw(Switch[0]))
  {
    case '@':
      ListMode=Switch[1]=='+' ? RCLM_ACCEPT_LISTS:RCLM_REJECT_LISTS;
      break;
    case 'A':
      switch(toupperw(Switch[1]))
      {
        case 'C':
          ClearArc=true;
          break;
        case 'D':
          if (Switch[2]==0)
            AppendArcNameToPath=APPENDARCNAME_DESTPATH;
          else
            if (Switch[2]=='1')
              AppendArcNameToPath=APPENDARCNAME_OWNSUBDIR;
            else
              if (Switch[2]=='2')
                AppendArcNameToPath=APPENDARCNAME_OWNDIR;
          break;
#ifndef SFX_MODULE
        case 'G':
          if (Switch[2]=='-' && Switch[3]==0)
            GenerateArcName=0;
          else
            if (toupperw(Switch[2])=='F')
              wcsncpyz(DefGenerateMask,Switch+3,ASIZE(DefGenerateMask));
            else
            {
              GenerateArcName=true;
              wcsncpyz(GenerateMask,Switch+2,ASIZE(GenerateMask));
            }
          break;
#endif
        case 'I':
          IgnoreGeneralAttr=true;
          break;
        case 'M':
          switch(toupperw(Switch[2]))
          {
            case 0:
            case 'S':
              ArcMetadata=ARCMETA_SAVE;
              break;
            case 'R':
              ArcMetadata=ARCMETA_RESTORE;
              break;
            default:
              BadSwitch(Switch);
              break;
          }
          break;
        case 'O':
          AddArcOnly=true;
          break;
        case 'P':
          // Convert slashes here than before every comparison.
          SlashToNative(Switch+2,ArcPath);
          break;
        case 'S':
          SyncFiles=true;
          break;
        default:
          BadSwitch(Switch);
          break;
      }
      break;
    case 'C':
      if (Switch[2]!=0)
      {
          if (wcsicomp(Switch+1,L"FG-")==0)
            ConfigDisabled=true;
          else
            BadSwitch(Switch);
      }
      else
        switch(toupperw(Switch[1]))
        {
          case '-':
            DisableComment=true;
            break;
          case 'U':
            ConvertNames=NAMES_UPPERCASE;
            break;
          case 'L':
            ConvertNames=NAMES_LOWERCASE;
            break;
          default:
            BadSwitch(Switch);
            break;
        }
      break;
    case 'D':
      if (Switch[2]!=0)
        BadSwitch(Switch);
      else
        switch(toupperw(Switch[1]))
        {
          case 'S':
            DisableSortSolid=true;
            break;
          case 'H':
            OpenShared=true;
            break;
          case 'F':
            DeleteFiles=true;
            break;
          default:
            BadSwitch(Switch);
            break;
        }
      break;
    case 'E':
      switch(toupperw(Switch[1]))
      {
        case 'P':
          switch(Switch[2])
          {
            case 0:
              ExclPath=EXCL_SKIPWHOLEPATH;
              break;
            case '1':
              ExclPath=EXCL_BASEPATH;
              break;
            case '2':
              ExclPath=EXCL_SAVEFULLPATH;
              break;
            case '3':
              ExclPath=EXCL_ABSPATH;
              break;
            case '4':
              // Convert slashes here than before every comparison.
              SlashToNative(Switch+3,ExclArcPath);
              break;
            default:
              BadSwitch(Switch);
              break;
          }
          break;
        default:
          if (Switch[1]=='+')
          {
            InclFileAttr|=GetExclAttr(Switch+2,InclDir);
            InclAttrSet=true;
          }
          else
            ExclFileAttr|=GetExclAttr(Switch+1,ExclDir);
          break;
      }
      break;
    case 'F':
      if (Switch[1]==0)
        FreshFiles=true;
      else
        BadSwitch(Switch);
      break;
    case 'H':
      switch (toupperw(Switch[1]))
      {
        case 'P':
          EncryptHeaders=true;
          if (Switch[2]!=0)
          {
            if (wcslen(Switch+2)>=MAXPASSWORD)
              uiMsg(UIERROR_TRUNCPSW,MAXPASSWORD-1);
            Password.Set(Switch+2);
            cleandata((void *)Switch,wcslen(Switch)*sizeof(Switch[0]));
          }
          else
            if (!Password.IsSet())
            {
              uiGetPassword(UIPASSWORD_GLOBAL,L"",&Password,NULL);
              eprintf(L"\n");
            }
          break;
        default :
          BadSwitch(Switch);
          break;
      }
      break;
    case 'I':
      if (wcsnicomp(Switch+1,L"LOG",3)==0)
      {
        LogName=Switch[4]!=0 ? Switch+4:DefLogName;
        break;
      }
      if (wcsnicomp(Switch+1,L"SND",3)==0)
      {
        Sound=Switch[4]=='-' ? SOUND_NOTIFY_OFF : SOUND_NOTIFY_ON;
        break;
      }
      if (wcsicomp(Switch+1,L"ERR")==0)
      {
        MsgStream=MSG_STDERR;
        // Set it immediately when parsing the command line, so it also
        // affects messages issued while parsing the command line.
        SetConsoleMsgStream(MSG_STDERR);
        break;
      }
      if (wcsnicomp(Switch+1,L"EML",3)==0)
      {
        EmailTo=Switch[4]!=0 ? Switch+4:L"@";
        break;
      }
      if (wcsicomp(Switch+1,L"M")==0) // For compatibility with pre-WinRAR 6.0 -im syntax. Replaced with -idv.
      {
        VerboseOutput=true;
        break;
      }
      if (wcsicomp(Switch+1,L"NUL")==0)
      {
        MsgStream=MSG_NULL;
        SetConsoleMsgStream(MSG_NULL);
        break;
      }
      if (toupperw(Switch[1])=='D')
      {
        for (uint I=2;Switch[I]!=0;I++)
          switch(toupperw(Switch[I]))
          {
            case 'Q':
              MsgStream=MSG_ERRONLY;
              SetConsoleMsgStream(MSG_ERRONLY);
              break;
            case 'C':
              DisableCopyright=true;
              break;
            case 'D':
              DisableDone=true;
              break;
            case 'P':
              DisablePercentage=true;
              break;
            case 'N':
              DisableNames=true;
              break;
            case 'V':
              VerboseOutput=true;
              break;
          }
        break;
      }
      if (wcsnicomp(Switch+1,L"OFF",3)==0)
      {
        switch(Switch[4])
        {
          case 0:
          case '1':
            Shutdown=POWERMODE_OFF;
            break;
          case '2':
            Shutdown=POWERMODE_HIBERNATE;
            break;
          case '3':
            Shutdown=POWERMODE_SLEEP;
            break;
          case '4':
            Shutdown=POWERMODE_RESTART;
            break;
        }
        break;
      }
      if (wcsicomp(Switch+1,L"VER")==0)
      {
        PrintVersion=true;
        break;
      }
      break;
    case 'K':
      switch(toupperw(Switch[1]))
      {
        case 'B':
          KeepBroken=true;
          break;
        case 0:
          Lock=true;
          break;
      }
      break;
    case 'M':
      switch(toupperw(Switch[1]))
      {
        case 'C':
          {
            const wchar *Str=Switch+2;
            if (*Str=='-')
              for (uint I=0;I<ASIZE(FilterModes);I++)
                FilterModes[I].State=FILTER_DISABLE;
            else
              while (*Str!=0)
              {
                int Param1=0,Param2=0;
                FilterState State=FILTER_AUTO;
                FilterType Type=FILTER_NONE;
                if (IsDigit(*Str))
                {
                  Param1=atoiw(Str);
                  while (IsDigit(*Str))
                    Str++;
                }
                if (*Str==':' && IsDigit(Str[1]))
                {
                  Param2=atoiw(++Str);
                  while (IsDigit(*Str))
                    Str++;
                }
                switch(toupperw(*(Str++)))
                {
//                  case 'T': Type=FILTER_TEXT;        break;
                  case 'E': Type=FILTER_E8;          break;
                  case 'D': Type=FILTER_DELTA;       break;
//                  case 'A': Type=FILTER_AUDIO;       break;
//                  case 'C': Type=FILTER_RGB;         break;
//                  case 'R': Type=FILTER_ARM;         break;
                  case 'L': Type=FILTER_LONGRANGE;   break;
                  case 'X': Type=FILTER_EXHAUSTIVE;  break;
                }
                if (*Str=='+' || *Str=='-')
                  State=*(Str++)=='+' ? FILTER_FORCE:FILTER_DISABLE;
                FilterModes[Type].State=State;
                FilterModes[Type].Param1=Param1;
                FilterModes[Type].Param2=Param2;
              }
            }
          break;
        case 'D':
          {
            bool SetDictLimit=toupperw(Switch[2])=='X';

            uint64 Size=atoiw(Switch+(SetDictLimit ? 3 : 2));
            wchar LastChar=toupperw(Switch[wcslen(Switch)-1]);
            if (IsDigit(LastChar))
              LastChar=SetDictLimit ? 'G':'M'; // Treat -md128 as -md128m and -mdx32 as -mdx32g.
            switch(LastChar)
            {
              case 'K':
                Size*=1024;
                break;
              case 'M':
                Size*=1024*1024;
                break;
              case 'G':
                Size*=1024*1024*1024;
                break;
              default:
                BadSwitch(Switch);
            }

            // 2023.07.22: For 4 GB and less we also check that it is power of 2,
            // so archives are compatible with RAR 5.0+.
            // We allow Size>PACK_MAX_DICT here, so we can use -md[x] to unpack
            // archives created by future versions with higher PACK_MAX_DICT�
            uint Flags;
            if ((Size=Archive::GetWinSize(Size,Flags))==0 ||
                Size<=0x100000000ULL && !IsPow2(Size))
              BadSwitch(Switch);
            else
              if (SetDictLimit)
                WinSizeLimit=Size;
              else
              {
                WinSize=Size;
              }
          }
          break;
        case 'E':
          if (toupperw(Switch[2])=='S' && Switch[3]==0)
            SkipEncrypted=true;
          break;
        case 'L':
          if (toupperw(Switch[2])=='P')
          {
            UseLargePages=true;
            if (!LargePageAlloc::IsPrivilegeAssigned() && LargePageAlloc::AssignConfirmation())
            {
              LargePageAlloc::AssignPrivilege();

              // Quit immediately. We do not want to interrupt the current copy
              // archive processing with reboot after assigning privilege.
              SetupComplete=true;
            }
          }
          break;
        case 'M':
          break;
        case 'S':
          GetBriefMaskList(Switch[2]==0 ? DefaultStoreList:Switch+2,StoreArgs);
          break;
#ifdef RAR_SMP
        case 'T':
          Threads=atoiw(Switch+2);
          if (Threads>MaxPoolThreads || Threads<1)
            BadSwitch(Switch);
          break;
#endif
        default:
          Method=Switch[1]-'0';
          if (Method>5 || Method<0)
            BadSwitch(Switch);
          break;
      }
      break;
    case 'N':
    case 'X':
      if (Switch[1]!=0)
      {
        StringList *Args=toupperw(Switch[0])=='N' ? &InclArgs:&ExclArgs;
        if (Switch[1]=='@' && !IsWildcard(Switch))
          ReadTextFile(Switch+2,Args,false,true,FilelistCharset,true,true,true);
        else
          Args->AddString(Switch+1);
      }
      break;
    case 'O':
      switch(toupperw(Switch[1]))
      {
        case '+':
          Overwrite=OVERWRITE_ALL;
          break;
        case '-':
          Overwrite=OVERWRITE_NONE;
          break;
        case 0:
          Overwrite=OVERWRITE_FORCE_ASK;
          break;
#ifdef _WIN_ALL
        case 'C':
          SetCompressedAttr=true;
          break;
#endif
        case 'H':
          SaveHardLinks=true;
          break;


#ifdef SAVE_LINKS
        case 'L':
          SaveSymLinks=true;
          for (uint I=2;Switch[I]!=0;I++)
            switch(toupperw(Switch[I]))
            {
              case 'A':
                AbsoluteLinks=true;
                break;
              case '-':
                SkipSymLinks=true;
                break;
              default:
                BadSwitch(Switch);
                break;
            }
          break;
#endif
#ifdef PROPAGATE_MOTW
        case 'M':
          {
            MotwAllFields=Switch[2]=='1';
            const wchar *Sep=wcschr(Switch+2,'=');
            if (Switch[2]=='-')
              MotwList.Reset();
            else
              GetBriefMaskList(Sep==nullptr ? L"*":Sep+1,MotwList);
          }
          break;
#endif
#ifdef _WIN_ALL
        case 'N':
          if (toupperw(Switch[2])=='I')
            AllowIncompatNames=true;
          break;
#endif
        case 'P':
          ExtrPath=Switch+2;
          AddEndSlash(ExtrPath);
          break;
        case 'R':
          Overwrite=OVERWRITE_AUTORENAME;
          break;
#ifdef _WIN_ALL
        case 'S':
          SaveStreams=true;
          break;
#endif
        case 'W':
          ProcessOwners=true;
          break;
        default :
          BadSwitch(Switch);
          break;
      }
      break;
    case 'P':
      if (Switch[1]==0)
      {
        uiGetPassword(UIPASSWORD_GLOBAL,L"",&Password,NULL);
        eprintf(L"\n");
      }
      else
      {
        if (wcslen(Switch+1)>=MAXPASSWORD)
          uiMsg(UIERROR_TRUNCPSW,MAXPASSWORD-1);
        Password.Set(Switch+1);
        cleandata((void *)Switch,wcslen(Switch)*sizeof(Switch[0]));
      }
      break;
#ifndef SFX_MODULE
    case 'Q':
      if (toupperw(Switch[1])=='O')
        switch(toupperw(Switch[2]))
        {
          case 0:
            QOpenMode=QOPEN_AUTO;
            break;
          case '-':
            QOpenMode=QOPEN_NONE;
            break;
          case '+':
            QOpenMode=QOPEN_ALWAYS;
            break;
          default:
            BadSwitch(Switch);
            break;
        }
      else
        BadSwitch(Switch);
      break;
#endif
    case 'R':
      switch(toupperw(Switch[1]))
      {
        case 0:
          Recurse=RECURSE_ALWAYS;
          break;
        case '-':
          Recurse=RECURSE_DISABLE;
          break;
        case '0':
          Recurse=RECURSE_WILDCARDS;
          break;
        case 'I':
          {
            Priority=atoiw(Switch+2);
            if (Priority<0 || Priority>15)
              BadSwitch(Switch);
            const wchar *ChPtr=wcschr(Switch+2,':');
            if (ChPtr!=NULL)
            {
              SleepTime=atoiw(ChPtr+1);
              if (SleepTime>1000)
                BadSwitch(Switch);
              InitSystemOptions(SleepTime);
            }
            SetPriority(Priority);
          }
          break;
      }
      break;
    case 'S':
      if (IsDigit(Switch[1]))
      {
        Solid|=SOLID_COUNT;
        SolidCount=atoiw(&Switch[1]);
      }
      else
        switch(toupperw(Switch[1]))
        {
          case 0:
            Solid|=SOLID_NORMAL;
            break;
          case '-':
            Solid=SOLID_NONE;
            break;
          case 'E':
            Solid|=SOLID_FILEEXT;
            break;
          case 'V':
            Solid|=Switch[2]=='-' ? SOLID_VOLUME_DEPENDENT:SOLID_VOLUME_INDEPENDENT;
            break;
          case 'D':
            Solid|=SOLID_VOLUME_DEPENDENT;
            break;
          case 'I':
            ProhibitConsoleInput();
            UseStdin=Switch[2] ? Switch+2:L"stdin";
            break;
          case 'L':
            if (IsDigit(Switch[2]))
              FileSizeLess=GetVolSize(Switch+2,1);
            break;
          case 'M':
            if (IsDigit(Switch[2]))
              FileSizeMore=GetVolSize(Switch+2,1);
            break;
          case 'C':
            {
              bool AlreadyBad=false; // Avoid reporting "bad switch" several times.

              RAR_CHARSET rch=RCH_DEFAULT;
              switch(toupperw(Switch[2]))
              {
                case 'A':
                  rch=RCH_ANSI;
                  break;
                case 'O':
                  rch=RCH_OEM;
                  break;
                case 'U':
                  rch=RCH_UNICODE;
                  break;
                case 'F':
                  rch=RCH_UTF8;
                  break;
                default :
                  BadSwitch(Switch);
                  AlreadyBad=true;
                  break;
              };
              if (!AlreadyBad)
                if (Switch[3]==0)
                  CommentCharset=FilelistCharset=ErrlogCharset=RedirectCharset=rch;
                else
                  for (uint I=3;Switch[I]!=0 && !AlreadyBad;I++)
                    switch(toupperw(Switch[I]))
                    {
                      case 'C':
                        CommentCharset=rch;
                        break;
                      case 'L':
                        FilelistCharset=rch;
                        break;
                      case 'R':
                        RedirectCharset=rch;
                        break;
                      default:
                        BadSwitch(Switch);
                        AlreadyBad=true;
                        break;
                    }
              // Set it immediately when parsing the command line, so it also
              // affects messages issued while parsing the command line.
              SetConsoleRedirectCharset(RedirectCharset);
            }
            break;

        }
      break;
    case 'T':
      switch(toupperw(Switch[1]))
      {
        case 'K':
          ArcTime=ARCTIME_KEEP;
          break;
        case 'L':
          ArcTime=ARCTIME_LATEST;
          break;
        case 'O':
          SetTimeFilters(Switch+2,true,true);
          break;
        case 'N':
          SetTimeFilters(Switch+2,false,true);
          break;
        case 'B':
          SetTimeFilters(Switch+2,true,false);
          break;
        case 'A':
          SetTimeFilters(Switch+2,false,false);
          break;
        case 'S':
          SetStoreTimeMode(Switch+2);
          break;
        case '-':
          Test=false;
          break;
        case 0:
          Test=true;
          break;
        default:
          BadSwitch(Switch);
          break;
      }
      break;
    case 'U':
      if (Switch[1]==0)
        UpdateFiles=true;
      else
        BadSwitch(Switch);
      break;
    case 'V':
      switch(toupperw(Switch[1]))
      {
        case 'P':
          VolumePause=true;
          break;
        case 'E':
          if (toupperw(Switch[2])=='R')
            VersionControl=atoiw(Switch+3)+1;
          break;
        case '-':
          VolSize=0;
          break;
        default:
          VolSize=VOLSIZE_AUTO; // UnRAR -v switch for list command.
          break;
      }
      break;
    case 'W':
      TempPath=Switch+1;
      AddEndSlash(TempPath);
      break;
    case 'Y':
      AllYes=true;
      break;
    case 'Z':
      if (Switch[1]==0)
      {
        // If comment file is not specified, we read data from stdin.
        CommentFile=L"stdin";
      }
      else
        CommentFile=Switch+1;
      break;
    case '?' :
      OutHelp(RARX_SUCCESS);
      break;
    default :
      BadSwitch(Switch);
      break;
  }
}
#endif


#if !defined(SFX_MODULE)
void CommandData::BadSwitch(const wchar *Switch)
{
  mprintf(St(MUnknownOption),Switch);
  ErrHandler.Exit(RARX_USERERROR);
}
#endif


void CommandData::ProcessCommand()
{
#ifndef SFX_MODULE

  const wchar *SingleCharCommands=L"FUADPXETK";

  // RAR -mlp command is the legitimate way to assign the required privilege.
  if (Command.empty() && UseLargePages || SetupComplete)
    return;

  if (Command[0]!=0 && Command[1]!=0 && wcschr(SingleCharCommands,Command[0])!=NULL || ArcName.empty())
    OutHelp(Command.empty() ? RARX_SUCCESS:RARX_USERERROR); // Return 'success' for 'rar' without parameters.

  size_t ExtPos=GetExtPos(ArcName);
#ifdef _UNIX
  // If we want to update an archive without extension, in Windows we can use
  // "arcname." and it will be treated as "arcname". In Unix "arcname"
  // and "arcname." are two different names, so we check if "arcname" exists
  // and do not append ".rar", allowing user to update such archive.
  if (ExtPos==std::wstring::npos && (!FileExist(ArcName) || IsDir(GetFileAttr(ArcName))))
    ArcName+=L".rar";
#else
  if (ExtPos==std::wstring::npos)
    ArcName+=L".rar";
#endif
  // Treat arcname.part1 as arcname.part1.rar.
  if (ExtPos!=std::wstring::npos && wcsnicomp(&ArcName[ExtPos],L".part",5)==0 &&
      IsDigit(ArcName[ExtPos+5]) && !FileExist(ArcName))
  {
    std::wstring Name=ArcName+L".rar";
    if (FileExist(Name))
      ArcName=Name;
  }

  if (wcschr(L"AFUMD",Command[0])==NULL && UseStdin.empty())
  {
    if (GenerateArcName)
    {
      const wchar *Mask=*GenerateMask!=0 ? GenerateMask:DefGenerateMask;
      GenerateArchiveName(ArcName,Mask,false);
    }

    StringList ArcMasks;
    ArcMasks.AddString(ArcName);
    ScanTree Scan(&ArcMasks,Recurse,SaveSymLinks,SCAN_SKIPDIRS);
    FindData FindData;
    while (Scan.GetNext(&FindData)==SCAN_SUCCESS)
      AddArcName(FindData.Name);
  }
  else
    AddArcName(ArcName);
#endif

  switch(Command[0])
  {
    case 'P':
    case 'X':
    case 'E':
    case 'T':
      {
        CmdExtract Extract(this);
        Extract.DoExtract();
      }
      break;
#ifndef SILENT
    case 'V':
    case 'L':
      ListArchive(this);
      break;
    default:
      OutHelp(RARX_USERERROR);
#endif
  }

  // Since messages usually include '\n' in the beginning, we also issue
  // the final '\n'. It is especially important in Unix, where otherwise
  // the shell can display the prompt on the same line as the last message.
  // mprintf is blocked with -idq and if error messages had been displayed
  // in this mode, we use eprintf to separate them from shell prompt.
  // If nothing was displayed with -idq, we avoid the excessive empty line.
  if (!BareOutput)
    if (MsgStream==MSG_ERRONLY && IsConsoleOutputPresent())
      eprintf(L"\n");
    else
      mprintf(L"\n");
}


void CommandData::AddArcName(const std::wstring &Name)
{
  ArcNames.AddString(Name);
}


bool CommandData::GetArcName(wchar *Name,int MaxSize)
{
  return ArcNames.GetString(Name,MaxSize);
}


bool CommandData::GetArcName(std::wstring &Name)
{
  return ArcNames.GetString(Name);
}


bool CommandData::IsSwitch(int Ch)
{
#ifdef _WIN_ALL
  return Ch=='-' || Ch=='/';
#else
  return Ch=='-';
#endif
}


#ifndef SFX_MODULE
uint CommandData::GetExclAttr(const wchar *Str,bool &Dir)
{
  if (IsDigit(*Str))
    return wcstol(Str,NULL,0);

  uint Attr=0;
  while (*Str!=0)
  {
    switch(toupperw(*Str))
    {
      case 'D':
        Dir=true;
        break;
#ifdef _UNIX
      case 'V':
        Attr|=S_IFCHR;
        break;
#elif defined(_WIN_ALL)
      case 'R':
        Attr|=0x1;
        break;
      case 'H':
        Attr|=0x2;
        break;
      case 'S':
        Attr|=0x4;
        break;
      case 'A':
        Attr|=0x20;
        break;
#endif
    }
    Str++;
  }
  return Attr;
}
#endif




#ifndef SFX_MODULE
void CommandData::ReportWrongSwitches(RARFORMAT Format)
{
  if (Format==RARFMT15)
  {
    if (HashType!=HASH_CRC32)
      uiMsg(UIERROR_INCOMPATSWITCH,L"-ht",4);
#ifdef _WIN_ALL
    if (SaveSymLinks)
      uiMsg(UIERROR_INCOMPATSWITCH,L"-ol",4);
#endif
    if (SaveHardLinks)
      uiMsg(UIERROR_INCOMPATSWITCH,L"-oh",4);

#ifdef _WIN_ALL
    // Do not report a wrong dictionary size here, because we are not sure
    // yet about archive format. We can switch to RAR5 mode later
    // if we update RAR5 archive.


#endif
    if (QOpenMode!=QOPEN_AUTO)
      uiMsg(UIERROR_INCOMPATSWITCH,L"-qo",4);
  }
  if (Format==RARFMT50)
  {
  }
}
#endif


int64 CommandData::GetVolSize(const wchar *S,uint DefMultiplier)
{
  int64 Size=0,FloatingDivider=0;
  for (uint I=0;S[I]!=0;I++)
    if (IsDigit(S[I]))
    {
      Size=Size*10+S[I]-'0';
      FloatingDivider*=10;
    }
    else
      if (S[I]=='.')
        FloatingDivider=1;

  if (*S!=0)
  {
    const wchar *ModList=L"bBkKmMgGtT";
    const wchar *Mod=wcschr(ModList,S[wcslen(S)-1]);
    if (Mod==NULL)
      Size*=DefMultiplier;
    else
      for (ptrdiff_t I=2;I<=Mod-ModList;I+=2)
        Size*=((Mod-ModList)&1)!=0 ? 1000:1024;
  }
  if (FloatingDivider!=0)
    Size/=FloatingDivider;
  return Size;
}


// Treat the list like rar;zip as *.rar;*.zip for -ms and similar switches.
void CommandData::GetBriefMaskList(const std::wstring &Masks,StringList &Args)
{
  size_t Pos=0;
  while (Pos<Masks.size())
  {
    if (Masks[Pos]=='.')
      Pos++;
    size_t EndPos=Masks.find(';',Pos);
    std::wstring Mask=Masks.substr(Pos,EndPos==std::wstring::npos ? EndPos:EndPos-Pos);
    if (Mask.find_first_of(L"*?.")==std::wstring::npos)
      Mask.insert(0,L"*.");
    Args.AddString(Mask);
    if (EndPos==std::wstring::npos)
      break;
    Pos=EndPos+1;
  }
}




