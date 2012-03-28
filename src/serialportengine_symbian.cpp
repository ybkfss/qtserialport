/*
    License...
*/

/*!
    \class SymbianSerialPortEngine
    \internal

    \brief The SymbianSerialPortEngine class provides *nix OS
    platform-specific low level access to a serial port.

    \reentrant
    \ingroup serialport-main
    \inmodule QtSerialPort

    Currently the class supports all?? version of Symbian OS.

    SymbianSerialPortEngine (as well as other platform-dependent engines)
    is a class with multiple inheritance, which on the one hand,
    derives from a general abstract class interface SerialPortEngine,
    on the other hand of a class inherited from QObject.

    From the abstract class SerialPortEngine, it inherits all virtual
    interface methods that are common to all serial ports on any platform.
    These methods, the class SymbianSerialPortEngine implements use
    Symbian API.

    From QObject-like class ...
    ...
    ...
    ...

    That is, as seen from the above, the functional SymbianSerialPortEngine
    completely covers all the necessary tasks.
*/

#include "serialportengine_symbian_p.h"

#include <e32base.h>
//#include <e32test.h>
#include <f32file.h>

#include <QtCore/qregexp.h>
//#include <QtCore/QDebug>


// Physical device driver.
#if defined (__WINS__)
_LIT(KPddName, "ECDRV");
#else // defined (__EPOC32__)
_LIT(KPddName, "EUART");
#endif

// Logical  device driver.
_LIT(KLddName,"ECOMM");

// Modules names.
_LIT(KRS232ModuleName, "ECUART");
_LIT(KBluetoothModuleName, "BTCOMM");
_LIT(KInfraRedModuleName, "IRCOMM");
_LIT(KACMModuleName, "ECACM");


// Return false on error load.
static bool loadDevices()
{
    TInt r = KErrNone;
#if defined (__WINS__)
    RFs fileServer;
    r = User::LeaveIfError(fileServer.Connect());
    if (r != KErrNone)
        return false;
    fileServer.Close ();
#endif

    r = User::LoadPhysicalDevice(KPddName);
    if ((r != KErrNone) && (r != KErrAlreadyExists))
        return false; //User::Leave(r);

    r = User::LoadLogicalDevice(KLddName);
    if ((r != KErrNone) && (r != KErrAlreadyExists))
        return false; //User::Leave(r);

#if !defined (__WINS__)
    r = StartC32();
    if ((r != KErrNone) && (r != KErrAlreadyExists))
        return false; //User::Leave(r);
#endif

    return true;
}

QT_BEGIN_NAMESPACE_SERIALPORT

/* Public methods */

/*!
    Constructs a SymbianSerialPortEngine with \a parent and
    initializes all the internal variables of the initial values.

    A pointer \a parent to the object class SerialPortPrivate
    is required for the recursive call some of its methods.
*/
SymbianSerialPortEngine::SymbianSerialPortEngine(SerialPortPrivate *d)
{
    Q_ASSERT(d);
    // Impl me
    dptr = d;
}

/*!
    Destructs a SymbianSerialPortEngine,
*/
SymbianSerialPortEngine::~SymbianSerialPortEngine()
{

}

/*!
    Tries to open the object desired serial port by \a location
    in the given open \a mode. In the API of Symbian there is no flag
    to open the port in r/o, w/o or r/w, most likely he always opens
    as r/w.

    Since the port in the Symbian OS can be open in any access mode,
    then this method forcibly puts a port in exclusive mode access.
    In the process of discovery, always set a port in non-blocking
    mode (when the read operation returns immediately) and tries to
    determine its current configuration and install them.

    If successful, returns true; otherwise false, with the setup a
    error code.
*/
bool SymbianSerialPortEngine::open(const QString &location, QIODevice::OpenMode mode)
{
    // Maybe need added check an ReadWrite open mode?
    Q_UNUSED(mode)

    if (!loadDevices()) {
        dptr->setError(SerialPort::UnknownPortError);
        return false;
    }

    RCommServ server;
    TInt r = server.Connect();
    if (r != KErrNone) {
        dptr->setError(SerialPort::UnknownPortError);
        return false;
    }

    if (location.contains("BTCOMM"))
        r = server.LoadCommModule(KBluetoothModuleName);
    else if (location.contains("IRCOMM"))
        r = server.LoadCommModule(KInfraRedModuleName);
    else if (location.contains("ACM"))
        r = server.LoadCommModule(KACMModuleName);
    else
        r = server.LoadCommModule(KRS232ModuleName);

    if (r != KErrNone) {
        dptr->setError(SerialPort::UnknownPortError);
        return false;
    }

    // In Symbian OS port opening only in R/W mode !?
    TPtrC portName(static_cast<const TUint16*>(location.utf16()), location.length());
    r = m_descriptor.Open(server, portName, ECommExclusive);

    if (r != KErrNone) {
        switch (r) {
        case KErrPermissionDenied:
            dptr->setError(SerialPort::NoSuchDeviceError); break;
        case KErrLocked:
        case KErrAccessDenied:
            dptr->setError(SerialPort::PermissionDeniedError); break;
        default:
            dptr->setError(SerialPort::UnknownPortError);
        }
        return false;
    }

    // Save current port settings.
    r = m_descriptor.Config(m_restoredSettings);
    if (r != KErrNone) {
        dptr->setError(SerialPort::UnknownPortError);
        return false;
    }

    detectDefaultSettings();
    return true;
}

/*!
    Closes a serial port object. Before closing restore previous
    serial port settings if necessary.
*/
void SymbianSerialPortEngine::close(const QString &location)
{
    Q_UNUSED(location);

    if (dptr->options.restoreSettingsOnClose) {
        m_descriptor.SetConfig(m_restoredSettings);
    }

    m_descriptor.Close();
}

/*!
    Returns a bitmap state of RS-232 line signals. On error,
    bitmap will be empty (equal zero).

    Symbian API allows you to receive only the state of signals:
    CTS, DSR, DCD, RING, RTS, DTR. Other signals are not available.
*/
SerialPort::Lines SymbianSerialPortEngine::lines() const
{
    SerialPort::Lines ret = 0;

    TUint signalMask = 0;
    m_descriptor.Signals(signalMask);

    if (signalMask & KSignalCTS)
        ret |= SerialPort::Cts;
    if (signalMask & KSignalDSR)
        ret |= SerialPort::Dsr;
    if (signalMask & KSignalDCD)
        ret |= SerialPort::Dcd;
    if (signalMask & KSignalRNG)
        ret |= SerialPort::Ri;
    if (signalMask & KSignalRTS)
        ret |= SerialPort::Rts;
    if (signalMask & KSignalDTR)
        ret |= SerialPort::Dtr;

    //if (signalMask & KSignalBreak)
    //  ret |=
    return ret;
}

/*!
    Set DTR signal to state \a set.


*/
bool SymbianSerialPortEngine::setDtr(bool set)
{
    TInt r;
    if (set)
        r = m_descriptor.SetSignalsToMark(KSignalDTR);
    else
        r = m_descriptor.SetSignalsToSpace(KSignalDTR);

    return (r == KErrNone);
}

/*!
    Set RTS signal to state \a set.


*/
bool SymbianSerialPortEngine::setRts(bool set)
{
    TInt r;
    if (set)
        r = m_descriptor.SetSignalsToMark(KSignalRTS);
    else
        r = m_descriptor.SetSignalsToSpace(KSignalRTS);

    return (r == KErrNone);
}

/*!

*/
bool SymbianSerialPortEngine::flush()
{
    // Impl me
    return false;
}

/*!
    Resets the transmit and receive serial port buffers
    independently.
*/
bool SymbianSerialPortEngine::reset()
{
    TInt r = m_descriptor.ResetBuffers(KCommResetRx | KCommResetTx);
    return (r == KErrNone);
}

/*!
    Sets a break condition for a specified time \a duration
    in milliseconds.

    A break condition on a line is when a data line is held
    permanently high for an indeterminate period which must be
    greater than the time normally taken to transmit two characters.
    It is sometimes used as an error signal between computers and
    other devices attached to them over RS232 lines.

    Setting breaks is not supported on the integral ARM
    serial hardware. EPOC has no support for detecting received
    breaks. There is no way to detects whether setting a break is
    supported using Caps().
*/
bool SymbianSerialPortEngine::sendBreak(int duration)
{
    TRequestStatus status;
    m_descriptor.Break(status, TTimeIntervalMicroSeconds32(duration * 1000));
    return false;
}

/*!

*/
bool SymbianSerialPortEngine::setBreak(bool set)
{
    // Impl me
    return false;
}

/*!
    Gets the number of bytes currently waiting in the
    driver's input buffer. A return value of zero means
    the buffer is empty.
*/
qint64 SymbianSerialPortEngine::bytesAvailable() const
{
    return qint64(m_descriptor.QueryReceiveBuffer());
}

/*!

    It is not possible to find out exactly how many bytes are
    currently in the driver's output buffer waiting to be
    transmitted. However, this is not an issue since it is easy
    to ensure that the output buffer is empty. If the
    KConfigWriteBufferedComplete bit (set via the TCommConfigV01
    structure's iHandshake field) is clear, then all write
    requests will delay completion until the data has completely
    cleared the driver's output buffer.
    If the KConfigWriteBufferedComplete bit is set, a write of zero
    bytes to a port which has data in the output buffer is guaranteed
    to delay completion until the buffer has been fully drained.

*/
qint64 SymbianSerialPortEngine::bytesToWrite() const
{
    // Impl me
    return 0;
}

/*!

    Reads data from a serial port only if it arrives before a
    specified time-out (zero). All reads from the serial device
    use 8-bit m_descriptors as data buffers, even on a Unicode system.

    The length of the TDes8 is set to zero on entry, which means that
    buffers can be reused without having to be zeroed first.

    The number of bytes to read is set to the maximum length of the
    m_descriptor.

    If a read is issued with a data length of zero the Read() completes
    immediately but with the side effect that the serial hardware is
    powered up.

    When a Read() terminates with KErrTimedOut, different protocol
    modules can show different behaviours. Some may write any data
    received into the aDes buffer, while others may return just an
    empty m_descriptor. In the case of a returned empty m_descriptor use
    ReadOneOrMore() to read any data left in the buffer.

    The behaviour of this API after a call to NotifyDataAvailable() is
    not prescribed and so different CSY's behave differently. IrComm
    will allow a successful completion of this API after a call to
    NotifyDataAvailable(), while ECUART and ECACM will complete the
    request with KErrInUse.

*/
qint64 SymbianSerialPortEngine::read(char *data, qint64 len)
{
    TPtr8 buffer((TUint8 *)data, (int)len);
    TRequestStatus status;
    m_descriptor.Read(status, TTimeIntervalMicroSeconds32(0), buffer);
    User::WaitForRequest(status);
    TInt err = status.Int();
    if (err != KErrNone) {
        dptr->setError(SerialPort::IoError);
        return qint64(-1);
    }
    return qint64(buffer.Length());
}

/*!

    Writes data to a serial port. All writes to the serial device
    use 8-bit m_descriptors as data buffers, even on a Unicode system.

    The number of bytes to write is set to the maximum length of
    the m_descriptor.

    When a Write() is issued with a data length of zero it cannot
    complete until the current handshaking configuration and the
    state of input control lines indicate that it is possible for
    data to be immediately written to the serial line, even though no
    data is to be written. This functionality is useful when
    determining when serial devices come on line, and checking that
    the output buffer is empty (if the KConfigWriteBufferedComplete
    bit is set).

*/
qint64 SymbianSerialPortEngine::write(const char *data, qint64 len)
{
    TPtrC8 buffer((TUint8*)data, (int)len);
    TRequestStatus status;
    m_descriptor.Write(status, buffer);
    User::WaitForRequest(status);
    TInt err = status.Int();

    if (err != KErrNone) {
        dptr->setError(SerialPort::IoError);
        len = -1;
    }
    // FIXME: How to get the actual number of bytes written?
    return qint64(len);
}

/*!

*/
bool SymbianSerialPortEngine::select(int timeout,
                                     bool checkRead, bool checkWrite,
                                     bool *selectForRead, bool *selectForWrite)
{

    // FIXME: I'm not sure in implementation this method.
    // Someone needs to check and correct.

    TRequestStatus timerStatus;
    TRequestStatus readStatus;
    TRequestStatus writeStatus;

    if (timeout > 0)  {
        if (!m_selectTimer.Handle()) {
            if (m_selectTimer.CreateLocal() != KErrNone)
                return false;
        }
        m_selectTimer.HighRes(timerStatus, timeout * 1000);
    }

    if (checkRead)
        m_descriptor.NotifyDataAvailable(readStatus);

    if (checkWrite)
        m_descriptor.NotifyOutputEmpty(writeStatus);

    enum { STATUSES_COUNT = 3 };
    TRequestStatus *statuses[STATUSES_COUNT];
    TInt num = 0;
    statuses[num++] = &timerStatus;
    statuses[num++] = &readStatus;
    statuses[num++] = &writeStatus;

    User::WaitForNRequest(statuses, num);

    bool result = false;

    // By timeout?
    if (timerStatus != KRequestPending) {
        Q_ASSERT(selectForRead);
        *selectForRead = false;
        Q_ASSERT(selectForWrite);
        *selectForWrite = false;
    } else {
        m_selectTimer.Cancel();
        User::WaitForRequest(timerStatus);

        // By read?
        if (readStatus != KRequestPending) {
            Q_ASSERT(selectForRead);
            *selectForRead = true;
        }

        // By write?
        if (writeStatus != KRequestPending) {
            Q_ASSERT(selectForWrite);
            *selectForWrite = true;
        }

        if (checkRead)
            m_descriptor.NotifyDataAvailableCancel();
        if (checkWrite)
            m_descriptor.NotifyOutputEmptyCancel();

        result = true;
    }
    return result;
}

//static const QString defaultPathPostfix = ":";

/*!
    Converts a platform specific \a port name to system location
    and return result.

    Does not do anything because These concepts are equivalent.
*/
QString SymbianSerialPortEngine::toSystemLocation(const QString &port) const
{
    // Port name is equval to port location.
    return port;
}

/*!
    Converts a platform specific system \a location to port name
    and return result.

    Does not do anything because These concepts are equivalent.
*/
QString SymbianSerialPortEngine::fromSystemLocation(const QString &location) const
{
    // Port name is equval to port location.
    return location;
}

/*!
    Set desired \a rate by given direction \a dir.
    However, Symbian does not support separate directions, so the
    method will return an error. Also it supports only the standard
    set of speed.

    If successful, returns true; otherwise false, with the setup a
    error code.
*/
bool SymbianSerialPortEngine::setRate(qint32 rate, SerialPort::Directions dir)
{
    if (dir != SerialPort::AllDirections) {
        dptr->setError(SerialPort::UnsupportedPortOperationError);
        return false;
    }

    rate = settingFromRate(rate);
    if (rate)
        m_currentSettings().iRate = static_cast<TBps>(rate);
    else {
        dptr->setError(SerialPort::UnsupportedPortOperationError);
        return false;
    }

    return updateCommConfig();
}

/*!
    Set desired number of data bits \a dataBits in byte. Symbian
    native supported all present number of data bits 5, 6, 7, 8.

    If successful, returns true; otherwise false, with the setup a
    error code.
*/
bool SymbianSerialPortEngine::setDataBits(SerialPort::DataBits dataBits)
{
    switch (dataBits) {
    case SerialPort::Data5:
        m_currentSettings().iDataBits = EData5;
        break;
    case SerialPort::Data6:
        m_currentSettings().iDataBits = EData6;
        break;
    case SerialPort::Data7:
        m_currentSettings().iDataBits = EData7;
        break;
    case SerialPort::Data8:
        m_currentSettings().iDataBits = EData8;
        break;
    default:
        dptr->setError(SerialPort::UnsupportedPortOperationError);
        return false;
    }

    return updateCommConfig();
}

/*!
    Set desired \a parity control mode. Symbian native supported
    all present parity types no parity, space, mark, even, odd.

    If successful, returns true; otherwise false, with the setup a
    error code.
*/
bool SymbianSerialPortEngine::setParity(SerialPort::Parity parity)
{
    switch (parity) {
    case SerialPort::NoParity:
        m_currentSettings().iParity = EParityNone;
        break;
    case SerialPort::EvenParity:
        m_currentSettings().iParity = EParityEven;
        break;
    case SerialPort::OddParity:
        m_currentSettings().iParity = EParityOdd;
        break;
    case SerialPort::MarkParity:
        m_currentSettings().iParity = EParityMark;
        break;
    case SerialPort::SpaceParity:
        m_currentSettings().iParity = EParitySpace;
        break;
    default:
        dptr->setError(SerialPort::UnsupportedPortOperationError);
        return false;
    }

    return updateCommConfig();
}

/*!
    Set desired number of stop bits \a stopBits in frame. Symbian
    native supported only 1, 2 number of stop bits.

    If successful, returns true; otherwise false, with the setup a
    error code.
*/
bool SymbianSerialPortEngine::setStopBits(SerialPort::StopBits stopBits)
{
    switch (stopBits) {
    case SerialPort::OneStop:
        m_currentSettings().iStopBits = EStop1;
        break;
    case SerialPort::TwoStop:
        m_currentSettings().iStopBits = EStop2;
        break;
    default:
        dptr->setError(SerialPort::UnsupportedPortOperationError);
        return false;
    }

    return updateCommConfig();
}

/*!
    Set desired \a flow control mode. Symbian native supported all
    present flow control modes no control, hardware (RTS/CTS),
    software (XON/XOFF).

    If successful, returns true; otherwise false, with the setup a
    error code.
*/
bool SymbianSerialPortEngine::setFlowControl(SerialPort::FlowControl flow)
{
    switch (flow) {
    case SerialPort::NoFlowControl:
        m_currentSettings().iHandshake = KConfigFailDSR;
        break;
    case SerialPort::HardwareControl:
        m_currentSettings().iHandshake = KConfigObeyCTS | KConfigFreeRTS;
        break;
    case SerialPort::SoftwareControl:
        m_currentSettings().iHandshake = KConfigObeyXoff | KConfigSendXoff;
        break;
    default:
        dptr->setError(SerialPort::UnsupportedPortOperationError);
        return false;
    }

    return updateCommConfig();
}

/*!

*/
bool SymbianSerialPortEngine::setDataErrorPolicy(SerialPort::DataErrorPolicy policy)
{
    Q_UNUSED(policy)
    // Impl me
    return true;
}

/*!

*/
bool SymbianSerialPortEngine::isReadNotificationEnabled() const
{
    // Impl me
    return false;
}

/*!

*/
void SymbianSerialPortEngine::setReadNotificationEnabled(bool enable)
{
    Q_UNUSED(enable)
    // Impl me
}

/*!

*/
bool SymbianSerialPortEngine::isWriteNotificationEnabled() const
{
    // Impl me
    return false;
}

/*!

*/
void SymbianSerialPortEngine::setWriteNotificationEnabled(bool enable)
{
    Q_UNUSED(enable)
    // Impl me
}

/*!

*/
bool SymbianSerialPortEngine::processIOErrors()
{
    // Impl me
    return false;
}

/* Public static methods */

struct RatePair
{
   qint32 rate;    // The numerical value of baud rate.
   qint32 setting; // The OS-specific code of baud rate.
   bool operator<(const RatePair &other) const { return rate < other.rate; }
   bool operator==(const RatePair &other) const { return setting == other.setting; }
};

// This table contains correspondences standard pairs values of
// baud rates that are defined in files
// - d32comm.h for Symbian^3
// - d32public.h for Symbian SR1
static
const RatePair standardRatesTable[] =
{
    { 50, EBps50 },
    { 75, EBps75 },
    { 110, EBps110},
    { 134, EBps134 },
    { 150, EBps150 },
    { 300, EBps300 },
    { 600, EBps600 },
    { 1200, EBps1200 },
    { 1800, EBps1800 },
    { 2000, EBps2000 },
    { 2400, EBps2400 },
    { 3600, EBps3600 },
    { 4800, EBps4800 },
    { 7200, EBps7200 },
    { 9600, EBps9600 },
    { 19200, EBps19200 },
    { 38400, EBps38400 },
    { 57600, EBps57600 },
    { 115200, EBps115200 },
    { 230400, EBps230400 },
    { 460800, EBps460800 },
    { 576000, EBps576000 },
    { 921600, EBps921600 },
    { 1152000, EBps1152000 },
    //{ 1843200, EBps1843200 }, only for Symbian SR1
    { 4000000, EBps4000000 }
};

static const RatePair *standardRatesTable_end =
        standardRatesTable + sizeof(standardRatesTable)/sizeof(*standardRatesTable);

/*!
    Convert symbian-specific enum of baud rate to a numeric value.
    If the desired item is not found then returns 0.
*/
qint32 SymbianSerialPortEngine::rateFromSetting(EBps setting)
{
    const RatePair rp = {0, setting};
    const RatePair *ret = qFind(standardRatesTable, standardRatesTable_end, rp);
    return (ret != standardRatesTable_end) ? ret->rate : 0;
}

/*!
    Convert a numeric value of baud rate to symbian-specific enum.
    If the desired item is not found then returns 0.
*/
EBps SymbianSerialPortEngine::settingFromRate(qint32 rate)
{
    const RatePair rp = {rate, 0};
    const RatePair *ret = qBinaryFind(standardRatesTable, standardRatesTable_end, rp);
    return (ret != standardRatesTable_end) ? ret->setting : 0;
}

/*!
    Returns a list standard values of baud rates,
    enums are defined in
   - d32comm.h for Symbian^3
   - d32public.h for Symbian SR1.
*/
QList<qint32> SymbianSerialPortEngine::standardRates()
{
    QList<qint32> ret;
    for (const RatePair *it = standardRatesTable; it != standardRatesTable_end; ++it)
       ret.append(it->rate);
    return ret;
}

/* Protected methods */

/*!
    Attempts to determine the current settings of the serial port,
    wehn it opened. Used only in the method open().
*/
void SymbianSerialPortEngine::detectDefaultSettings()
{
    // Detect rate.
    dptr->options.inputRate = rateFromSetting(m_currentSettings().iRate);
    dptr->options.outputRate = dptr->options.inputRate;

    // Detect databits.
    switch (m_currentSettings().iDataBits) {
    case EData5:
        dptr->options.dataBits = SerialPort::Data5;
        break;
    case EData6:
        dptr->options.dataBits = SerialPort::Data6;
        break;
    case EData7:
        dptr->options.dataBits = SerialPort::Data7;
        break;
    case EData8:
        dptr->options.dataBits = SerialPort::Data8;
        break;
    default:
        dptr->options.dataBits = SerialPort::UnknownDataBits;
    }

    // Detect parity.
    switch (m_currentSettings().iParity) {
    case EParityNone:
        dptr->options.parity = SerialPort::NoParity;
        break;
    case EParityEven:
        dptr->options.parity = SerialPort::EvenParity;
        break;
    case EParityOdd:
        dptr->options.parity = SerialPort::OddParity;
        break;
    case EParityMark:
        dptr->options.parity = SerialPort::MarkParity;
        break;
    case EParitySpace:
        dptr->options.parity = SerialPort::SpaceParity;
        break;
    default:
        dptr->options.parity = SerialPort::UnknownParity;
    }

    // Detect stopbits.
    switch (m_currentSettings().iStopBits) {
    case EStop1:
        dptr->options.stopBits = SerialPort::OneStop;
        break;
    case EStop2:
        dptr->options.stopBits = SerialPort::TwoStop;
        break;
    default:
        dptr->options.stopBits = SerialPort::UnknownStopBits;
    }

    // Detect flow control.
    if ((m_currentSettings().iHandshake & (KConfigObeyXoff | KConfigSendXoff))
            == (KConfigObeyXoff | KConfigSendXoff))
        dptr->options.flow = SerialPort::SoftwareControl;
    else if ((m_currentSettings().iHandshake & (KConfigObeyCTS | KConfigFreeRTS))
             == (KConfigObeyCTS | KConfigFreeRTS))
        dptr->options.flow = SerialPort::HardwareControl;
    else if (m_currentSettings().iHandshake & KConfigFailDSR)
        dptr->options.flow = SerialPort::NoFlowControl;
    else
        dptr->options.flow = SerialPort::UnknownFlowControl;
}

/* Private methods */

/*!
    Updates the TCommConfig structure wehn changing of any the
    parameters a serial port.

    If successful, returns true; otherwise false.
*/
bool SymbianSerialPortEngine::updateCommConfig()
{
    if (m_descriptor.SetConfig(m_currentSettings) != KErrNone) {
        dptr->setError(SerialPort::UnsupportedPortOperationError);
        return false;
    }
    return true;
}

// From <serialportengine_p.h>
SerialPortEngine *SerialPortEngine::create(SerialPortPrivate *d)
{
    return new SymbianSerialPortEngine(d);
}

#include "moc_serialportengine_symbian_p.cpp"

QT_END_NAMESPACE_SERIALPORT

