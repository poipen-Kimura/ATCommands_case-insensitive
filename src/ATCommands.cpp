/*
 This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "ATCommands.h"

ATCommands::ATCommands():errorHandler(NULL)
{
}

void ATCommands::begin(Stream *stream, const at_command_t *commands, uint32_t size, const uint16_t bufferSize, const char *terminator, bool caseSensitive)
{
    this->caseSensitive = caseSensitive;
    this->serial = stream;
    //结束符
    this->term = terminator;
    //保留空间
    this->bufferString.reserve(bufferSize);
    //缓冲区大小
    this->bufferSize = bufferSize;
    //注册指令，指令结构体大小（用来计算结构体数组数量）
    registerCommands(commands, size);
    //初始化缓存
    clearBuffer();
}

/**
 * @brief parseCommand
 * Checks the incoming buffer to ensure it begins with AT and then seeks to
 * determine if the command is of type RUN, TEST, READ or WRITE.  For WRITE
 * commands the buffer is furthered parsed to extract parameters.  Once
 * all actions are complete and the command type is determined the command
 * is compared against the delcared array (atCommands) to find a matching
 * command name.  If a match is found the function is passed to the handler
 * for later execution.
 * @return true
 * @return false
 */
bool ATCommands::parseCommand()
{
    // AT+ 0,1,2 从+开始,指令按"+CMD"进行识别，而不是"CMD"，所以从2开始，支持“CMD格式指令”
    uint16_t pos = 2;
    uint8_t type;

    // validate input so that we act only when we have to
    // 比如仅收到了一个EOL和结束符，则bufferPos==0,此种情况不进行处理
    if (this->bufferPos == 0)
    {
        // fall through
        setDefaultHandler(NULL);
        return true;
    }

    //没有以AT开头,则丢弃改指令
    String cmdHead = this->bufferString.substring(0, 2);
    if (!this->caseSensitive)
    {
        cmdHead.toUpperCase();
    }

    if (cmdHead != "AT")
    {
        return false;
    }

    // i 是当前要进行指令类型判断的字符索引，pos是当前要提取的指令字符串的索引，跳出后两者不一致 pos=i+1
    for (uint16_t i = 2; i < this->bufferSize; i++)
    {
        // if we reach the null terminator and have not yet reached a state then we
        // assume this to be a RUN command
        if (this->bufferString[i] == '\0')
        {
            type = AT_COMMAND_RUN;
            //跳出循环
            break;
        }

        // 合法字符判定，若含非法字符则返回false
        // eliminate shenanigans
        if (isValidCmdChar(this->bufferString[i]) == 0)
        {
            return false;
        }

        // 如果包含=，则认为WRITE指令
        // 如果包含=? 则是TEST指令
        // 如果包含? 则认为是READ指令
        // determine command type
        if (this->bufferString[i] == '=')
        {
            // Is this a TEST or a WRITE command?
            if (this->bufferString[i + 1] == '?')
            {
                type = AT_COMMAND_TEST;
                break;
            }
            else
            {
                type = AT_COMMAND_WRITE;
                break;
            }
        }
        if (this->bufferString[i] == '?')
        {
            type = AT_COMMAND_READ;
            break;
        }

        pos++;
    }
    //指令全部内容
    this->command = this->bufferString.substring(2, pos);
    //指令类型
    this->AT_COMMAND_TYPE = type;
    //当前指令在所有指令数组中的索引
    int8_t cmdNumber = -1;

    // search for matching command in array
    for (uint8_t i = 0; i < this->numberOfCommands; i++)
    {
        if (command.equals(atCommands[i].at_cmdName) ||
            (!this->caseSensitive && command.equalsIgnoreCase(atCommands[i].at_cmdName)))
        {
            cmdNumber = i;
            break;
        }
    }

    // if we did not find a match there's no point in continuing
    if (cmdNumber == -1)
    {
        if (errorHandler != NULL)
        {
            (*errorHandler)(this);
            this->cancelErrorMsg = true;
        }

        clearBuffer();
        return false;
    }

    // 根据不同的类型将Handler指针赋值给处理器
    // handle the different commands
    switch (type)
    {
    case AT_COMMAND_RUN:
        setDefaultHandler(this->atCommands[cmdNumber].at_runCmd);
        goto process;
    case AT_COMMAND_READ:
        setDefaultHandler(this->atCommands[cmdNumber].at_readCmd);
        goto process;
    case AT_COMMAND_TEST:
        setDefaultHandler(this->atCommands[cmdNumber].at_testCmd);
        goto process;
    case AT_COMMAND_WRITE:
        // TODO:需要判定什么时候返回false
        if (parseParameters(pos))
        {
            setDefaultHandler(this->atCommands[cmdNumber].at_writeCmd);
            goto process;
        }
        return false;

    process:
        // future placeholder
        return true;
    }

    return true;
}

/**
 * @brief parseParameters
 * Called mainly by parseCommand as an extention to tokenize parameters
 * usually supplied in WRITE (eg AT+COMMAND=param1,param2) commands.  It makes
 * use of malloc so check above in parseCommand where we free it to keep things
 * neat.
 * @param pos
 * @return true
 * @return false
 */
bool ATCommands::parseParameters(uint16_t pos)
{

    this->bufferString = this->bufferString.substring(pos + 1, this->bufferSize - pos + 1);

    return true;
}

boolean ATCommands::hasNext()
{
    if (tokenPos < bufferSize)
    {
        return true;
    }
    else
    {
        return false;
    }
}

/**
 * @brief next
 * This is called by user functions to iterate through the tokenized parameters.
 * Returns NULL when there is nothing more.  Subsequent calls pretty much ensure
 * this goes in a loop but it is expected the user knows their own parameters so there
 * would be no need to exceed boundaries.
 * @return char*
 */
String ATCommands::next()
{
    // if we have reached the boundaries return null so
    // that the caller knows not to expect anything
    if (tokenPos >= this->bufferSize)
    {
        tokenPos = bufferSize;
        return "";
    }

    String result = "";
    int delimiterIndex = this->bufferString.indexOf(",", tokenPos);

    //如果未找到,分隔符,则表示是最后一个参数，取全部
    if (delimiterIndex == -1)
    {
        result = this->bufferString.substring(tokenPos);
        //下一个token的位置在末尾
        tokenPos = this->bufferSize;
        return result;
    }
    else
    {
        //取到分隔符的位置的前一个字符，tokenPos到 delimiterIndex-1 的字符
        result = this->bufferString.substring(tokenPos, delimiterIndex);
        //下一个参数的开始是分隔符的后一个字符
        tokenPos = delimiterIndex + 1;
        return result;
    }
}

/**
 * @brief update
 * Main function called by the loop.  Reads in available charactrers and writes
 * to the buffer.  When the line terminator is found continues to parse and eventually
 * process the command.依靠判定终止符位置，将一个完整的指令写入缓存，并调用parseCommand进行解析
 * @return AT_COMMANDS_ERRORS
 */
AT_COMMANDS_ERRORS ATCommands::update()
{
    AT_COMMANDS_ERRORS ret;
    if (serial == NULL)
    {
        return AT_COMMANDS_ERRORS::AT_COMMANDS_ERROR_NO_SERIAL;
    }

    while (serial->available() > 0)
    {
        int ch = serial->read();

#ifdef AT_COMMANDS_DEBUG
        Serial.print(F("Read: bufferSize="));
        Serial.print(this->bufferSize);
        Serial.print(F(" bufferPos="));
        Serial.print(bufferPos);
        Serial.print(F(" termPos="));
        Serial.print(termPos);
        if (ch < 32)
        {
            Serial.print(F(" ch=#"));
            Serial.print(ch);
        }
        else
        {
            Serial.print(" ch=[");
            Serial.print((char)ch);
            Serial.print(F("]"));
        }
        Serial.println();
#endif
        // 0意味字符串结束 ，<0 无意义？
        if (ch <= 0)
        {
            continue;
        }

        //缓冲区未满则继续写入，不写入终止符"\r\n"
        if (bufferPos < this->bufferSize)
        {
            writeToBuffer(ch);
        }
        //缓冲区已满则不再继续写入，每次收到的数据不能比缓冲区对，否则爆粗
        else
        {
#ifdef AT_COMMANDS_DEBUG
            Serial.println(F("--BUFFER OVERFLOW--"));
#endif
            clearBuffer();
            return AT_COMMANDS_ERRORS::AT_COMMANDS_ERROR_BUFFER_FULL;
        }

        // 未到达终止符，则继续读取下一个字符
        if (term[termPos] != ch)
        {
            termPos = 0;
            continue;
        }

        // 这里很巧妙
        // term[++termPos] == 0 ，表示没有下一个终止符，则终止符判定完成，进入指令解析过程
        // term[++termPos] != 0 ，表示有下一个终止符，++termPos，则跳过解析，继续下一次读取进行判定
        if (term[++termPos] == 0)
        {

#ifdef AT_COMMANDS_DEBUG
            Serial.print(F("Received: ["));
            for (uint32_t n = 0; n < this->bufferSize; n++)
            {
                Serial.print(this->bufferString[n]);
            }
            Serial.println(F("]"));
#endif

            //进行指令解析
            if (!parseCommand())
            {
                this->error();
                clearBuffer();
                // added by guoxinghua
                return AT_COMMANDS_ERRORS::AT_COMMANDS_ERROR_SYNTAX;
            }

            // process the command
            processCommand();

            // finally clear the buffer
            clearBuffer();
        }
    }
    // add by guoxinghua
    return AT_COMMANDS_ERRORS::AT_COMMANDS_SUCCESS;
}

/**
 * @brief writeToBuffer
 * writes the input to the buffer excluding line terminators
 * @param data
 */
void ATCommands::writeToBuffer(int data)
{
    // we don't write EOL to the buffer
    if ((char)data != 13 && (char)data != 10)
    {
        this->bufferString += (char)data;
        bufferPos++;
    }
}

/**
 * @brief setDefaultHandler
 * Sets the function handler (callback) on the user's side.
 * @param function
 */
void ATCommands::setDefaultHandler(bool (*function)(ATCommands *))
{
    this->defaultHandler = function;
}

/**
 * @brief setErrorHandler
 * Set the user-side function handler (callback) to be called in case of a command error.
 * @param function
 */
void ATCommands::setErrorHandler(void (*function)(ATCommands *))
{
    this->errorHandler = function;
}

/**
 * @brief processCommand
 * Invokes the defined handler to process (callback) the command on the user's side.
 */
void ATCommands::processCommand()
{
    if (defaultHandler != NULL)
        if ((*defaultHandler)(this))
        {
            this->ok();
        }
        else
        {
            this->error();
        }
}

/**
 * @brief registerCommands
 * Registers the user-supplied command array for use later in parseCommand
 * @param commands
 * @param size
 * @return true
 * @return false
 */
bool ATCommands::registerCommands(const at_command_t *commands, uint32_t size)
{
    atCommands = commands;
    numberOfCommands = (uint16_t)(size / sizeof(at_command_t));
    // added by guoxinghua
    return true;
}

/**
 * @brief clearBuffer
 * resets the buffer and other buffer-related variables
 */
void ATCommands::clearBuffer()
{
    // for (uint16_t i = 0; i < this->buffer->size; i++)
    this->bufferString = "";
    //？？？
    termPos = 0;
    //缓冲区位置
    bufferPos = 0;
    //参数开始位置
    tokenPos = 0;
}

/**
 * @brief getBuffer
 * returns the buffer string
 * @return String
 */
String ATCommands::getBuffer()
{
    return this->bufferString;
}

/**
 * @brief ok
 * prints OK to terminal
 *
 */
void ATCommands::ok()
{
    this->serial->println("OK");
}

/**
 * @brief error
 * prints ERROR to terminal
 *
 */
void ATCommands::error()
{
    if (this->cancelErrorMsg)
    {
        this->cancelErrorMsg = false;
        return;
    }    
    this->serial->println("ERROR");
}

/**
 * @brief isValidCmdChar
 * Hackish attempt at validating input commands
 * @param ch
 * @return int
 */
int ATCommands::isValidCmdChar(const char ch)
{
    // return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || (ch == '+') || (ch == '#') || (ch == '$') || (ch == '@') || (ch == '_') || (ch == '=') || (ch == '?');
    return (ch >= 0x20 && ch <= 0x7E);
}
