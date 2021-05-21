//===------ injector.cpp - insert sensitive yolk data into elf file ------===//
//
//
//===----------------------------------------------------------------------===//
//
// usages: ./injector elfFile, dataFile
//                                        
// dataFile contains the sensitive data
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <map>
#include <stdint.h>

int main(int argc, char *argv[]) {

  if (argc != 3) {
    printf("usage: ./injector elfFile, dataFile\n");
    return -1;
  }
  
  FILE *elfFile, *dataFile, *locationFile, *symbolFile;
  char in[100];
  std::string name;
  char *line = NULL;
  uint64_t value = 0, index;
  std::map<std::string, uint64_t> yolkValues;
  std::map<uint64_t, std::string> yolkSymbols;
  size_t len = 0;
  size_t nread;
  int n;
  uint64_t tmp;
  int res = 0, val;
  std::size_t pos;
  uint64_t textSecOff, textMemAddr, yolkMemAddr, yolkOffset, symIndex;
  std::string yolkName;


  // read yolk value from data file
  dataFile = fopen(argv[2], "r");
  if (dataFile == NULL) {
    printf("unable to open data file\n");
    return -1;
  }

  while ((nread = getline(&line, &len, dataFile)) != -1) {
    if (line[0] == EOF)
      break;
    printf("line is %s", line);
    sscanf(line, "%s %li", in, &value);
    printf("done sscanf\n");
    name.assign(in);
    printf("name : %s, value: 0x%lx\n", name.c_str(), value);
    yolkValues.insert(std::pair<std::string, uint64_t>(name, value));
  } 
  printf("finish getting data\n");
 
  char command[100];

  // read yolk symbols from pipe
  // needed when "readelf -r" truncates the yolk symbol
  sprintf(command, "%s %s %s", "readelf -s", argv[1], " | grep yolk | awk '{print $1 $8}' | sed 's/:/ /g'");  
  symbolFile = popen(command, "r");
  if (symbolFile == NULL) {
    printf("unable to open symbol file\n");
    return -1;
  }
  while ((nread = getline(&line, &len, symbolFile)) != -1) {
    if (line[0] == EOF)
      break;
    printf("line is %s", line);
#if 1
    sscanf(line, "%lu %s", &index, in);
    name.assign(in);
    printf("index: %lx, name: %s\n", index, name.c_str());
    yolkSymbols.insert(std::pair<uint64_t, std::string>(index, name));
#endif
  }

  printf("finish getting symbols\n");


  // modify elf file
  elfFile = fopen(argv[1], "rb+");
  if (elfFile == NULL) {
    printf("unable to open elf file\n");
    return -1;
  }

  sprintf(command, "%s %s %s", "readelf -S", argv[1], " |grep text | awk '{print $4, $5}'");
  locationFile = popen(command, "r");
  if (locationFile == NULL) {
    printf("unable to open location file\n");
    return -1;
  }
 
  // get .text memory location and file offset 
  if ((nread = getline(&line, &len, locationFile)) != -1) {
    if (line[0] == EOF)
      return -1;
    printf("line is %s", line);
    sscanf(line, "%lx %lx", &textMemAddr, &textSecOff);
    printf("textSecOff is %lx, textMemAddr is %lx\n", textMemAddr, textSecOff);
  }
  else {
    printf("unable to read location file\n");
    return -1;
  }

  pclose(locationFile);
  
  sprintf(command, "%s %s %s", "readelf -r", argv[1], " | tail -n +4 | awk '{print $6, $1, $2}'");
  locationFile = popen(command, "r");
  if (locationFile == NULL) {
    printf("unable to open location file\n");
    return -1;
  }

  // process each yolk variable 
  while ((nread = getline(&line, &len, locationFile)) != -1) {
    if (line[0] == EOF)
      break;
    printf("line is %s", line);

    sscanf(line, "%s %lx %lx", in, &yolkMemAddr, &symIndex);
    yolkName.assign(in);
    printf("yolkName is %s, yolkMemAddr is %lx, symIndex is %lx\n", yolkName.c_str(), yolkMemAddr, symIndex);
    yolkOffset = yolkMemAddr - textMemAddr + textSecOff;
    printf("yolkOffset is %lx\n", yolkOffset);
    fseek(elfFile, yolkOffset, SEEK_SET);
    printf("after fseek\n");
#if 1 
    if ((pos = yolkName.find("32")) != std::string::npos) {
      printf("32 yolk\n");
      name = yolkName.substr(pos+3);
      // get value to be written
      if (yolkValues.find(name) != yolkValues.end())
        tmp = yolkValues.find(name)->second;
      else {
        // "readelf -r" truncated the yolk name
        // use symbol table to get the right name
        index = symIndex >> 32;
        if (yolkSymbols.find(index) != yolkSymbols.end()) {
          yolkName = yolkSymbols.find(index)->second;
          if ((pos = yolkName.find("32")) != std::string::npos) {
            printf("32 yolk\n");
            name = yolkName.substr(pos+3);
            if (yolkValues.find(name) != yolkValues.end())
              tmp = yolkValues.find(name)->second;
            else
              return -1;
           }
           else
             return -1;
         }
         else
           return -1;
        }
      // write to elf file
      for (int i = 0; i < 4; i++) {
        val = tmp % 256;
        if (val == 0)
          fseek(elfFile, 1, SEEK_CUR);
        else {
          res = fputc(val, elfFile);
          if (res == EOF) {
            printf("unable to write to the elf file\n");
            return -1;
          }
        }

        printf("tmp is %lx\n", tmp);
        tmp /= 256;
        if (tmp == 0)
          break;
      }

    }
    else if ((pos = yolkName.find("64")) != std::string::npos) {
      printf("64 yolk\n");
      name = yolkName.substr(pos+3);
      // get value to be written
      if (yolkValues.find(name) != yolkValues.end())
        tmp = yolkValues.find(name)->second;
      else {
        // "readelf -r" truncted the yolk name
        // use symbol table to get the right name
        index = symIndex >> 32;
        printf("index is %lx\n", index);
        if (yolkSymbols.find(index) != yolkSymbols.end()) {
          yolkName = yolkSymbols.find(index)->second;
          if ((pos = yolkName.find("64")) != std::string::npos) {
            printf("64 yolk\n");
            name = yolkName.substr(pos+3);
            if (yolkValues.find(name) != yolkValues.end())
              tmp = yolkValues.find(name)->second;
            else
              return -1;
           }
           else
             return -1;
         }
         else
           return -1;
        }
      // write to elf file
      for (int i = 0; i < 8; i++) {
        val = tmp % 256;
        if (val == 0)
          fseek(elfFile, 1, SEEK_CUR);
        else {
          res = fputc(val, elfFile);
          if (res == EOF) {
            printf("unable to write to the elf file\n");
            return -1;
          }
        }

        printf("tmp is %lx\n", tmp);
        tmp /= 256;
        if (tmp == 0)
          break;
      }
    }
    else {
      printf("yolk name error\n");
      return -1;
    }

    
#endif
  }
 
  printf("done writing file\n");
  free(line);

  printf("close files\n");
  fclose(elfFile);
  fclose(dataFile);
  pclose(locationFile);
  pclose(symbolFile);

  return 0;
}
