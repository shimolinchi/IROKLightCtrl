#include "AuraController.h"

#include "Logger.h"

#include <OleAuto.h>

namespace als {
namespace {

using Microsoft::WRL::ComPtr;

HRESULT GetDispatchId(IDispatch* dispatch, const wchar_t* name, DISPID& id) {
    LPOLESTR names[] = {const_cast<LPOLESTR>(name)};
    return dispatch->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &id);
}

HRESULT Invoke(IDispatch* dispatch,
               const wchar_t* name,
               WORD flags,
               VARIANTARG* arguments,
               UINT argumentCount,
               VARIANT* result = nullptr) {
    if (!dispatch) {
        return E_POINTER;
    }
    DISPID id = DISPID_UNKNOWN;
    HRESULT hr = GetDispatchId(dispatch, name, id);
    if (FAILED(hr)) {
        return hr;
    }
    DISPPARAMS parameters{};
    parameters.rgvarg = arguments;
    parameters.cArgs = argumentCount;
    DISPID propertyPut = DISPID_PROPERTYPUT;
    if ((flags & DISPATCH_PROPERTYPUT) != 0) {
        parameters.rgdispidNamedArgs = &propertyPut;
        parameters.cNamedArgs = 1;
    }
    EXCEPINFO exception{};
    UINT argumentError = 0;
    return dispatch->Invoke(id,
                            IID_NULL,
                            LOCALE_USER_DEFAULT,
                            flags,
                            &parameters,
                            result,
                            &exception,
                            &argumentError);
}

HRESULT InvokeNoArgs(IDispatch* dispatch, const wchar_t* name) {
    return Invoke(dispatch, name, DISPATCH_METHOD, nullptr, 0, nullptr);
}

ComPtr<IDispatch> VariantDispatch(VARIANT& value) {
    ComPtr<IDispatch> result;
    if (value.vt == VT_DISPATCH && value.pdispVal) {
        result.Attach(value.pdispVal);
        value.vt = VT_EMPTY;
        value.pdispVal = nullptr;
    } else if (value.vt == VT_UNKNOWN && value.punkVal) {
        value.punkVal->QueryInterface(IID_PPV_ARGS(&result));
    }
    VariantClear(&value);
    return result;
}

ComPtr<IDispatch> GetDispatch(IDispatch* object, const wchar_t* property) {
    VARIANT result;
    VariantInit(&result);
    if (FAILED(Invoke(object, property, DISPATCH_PROPERTYGET, nullptr, 0, &result))) {
        VariantClear(&result);
        return {};
    }
    return VariantDispatch(result);
}

long GetLong(IDispatch* object, const wchar_t* property) {
    VARIANT result;
    VariantInit(&result);
    if (FAILED(Invoke(object, property, DISPATCH_PROPERTYGET, nullptr, 0, &result))) {
        VariantClear(&result);
        return 0;
    }
    VARIANT converted;
    VariantInit(&converted);
    const HRESULT hr = VariantChangeType(&converted, &result, 0, VT_I4);
    const long value = SUCCEEDED(hr) ? converted.lVal : 0;
    VariantClear(&converted);
    VariantClear(&result);
    return value;
}

std::wstring GetString(IDispatch* object, const wchar_t* property) {
    VARIANT result;
    VariantInit(&result);
    if (FAILED(Invoke(object, property, DISPATCH_PROPERTYGET, nullptr, 0, &result))) {
        VariantClear(&result);
        return {};
    }
    VARIANT converted;
    VariantInit(&converted);
    const HRESULT hr = VariantChangeType(&converted, &result, 0, VT_BSTR);
    std::wstring value = SUCCEEDED(hr) && converted.bstrVal ? converted.bstrVal : L"";
    VariantClear(&converted);
    VariantClear(&result);
    return value;
}

ComPtr<IDispatch> CollectionItem(IDispatch* collection, long index) {
    VARIANTARG argument;
    VariantInit(&argument);
    argument.vt = VT_I4;
    argument.lVal = index;
    VARIANT result;
    VariantInit(&result);
    HRESULT hr = Invoke(collection,
                        L"Item",
                        DISPATCH_METHOD | DISPATCH_PROPERTYGET,
                        &argument,
                        1,
                        &result);
    if (FAILED(hr)) {
        VariantClear(&result);
        return {};
    }
    return VariantDispatch(result);
}

HRESULT SetUnsignedProperty(IDispatch* object, const wchar_t* property, std::uint32_t value) {
    VARIANTARG argument;
    VariantInit(&argument);
    argument.vt = VT_UI4;
    argument.ulVal = value;
    return Invoke(object, property, DISPATCH_PROPERTYPUT, &argument, 1, nullptr);
}

HRESULT InvokeUnsigned(IDispatch* object,
                       const wchar_t* method,
                       std::uint32_t value,
                       VARIANT* result = nullptr) {
    VARIANTARG argument;
    VariantInit(&argument);
    argument.vt = VT_UI4;
    argument.ulVal = value;
    return Invoke(object, method, DISPATCH_METHOD, &argument, 1, result);
}

}  // namespace

AuraController::~AuraController() {
    Stop();
}

void AuraController::Start() {
    if (thread_.joinable()) {
        return;
    }
    stop_.store(false);
    thread_ = std::thread(&AuraController::ThreadMain, this);
}

void AuraController::Stop() {
    stop_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    ready_.store(false);
}

void AuraController::SubmitColor(RgbColor color) {
    latestColor_.store(color.Packed());
    generation_.fetch_add(1);
}

std::wstring AuraController::Status() const {
    std::lock_guard lock(statusMutex_);
    return status_;
}

void AuraController::SetStatus(const std::wstring& value) {
    std::lock_guard lock(statusMutex_);
    status_ = value;
}

void AuraController::ThreadMain() {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        SetStatus(L"COM initialization failed");
        Logger::Instance().Error(L"Aura COM initialization failed: " + HResultMessage(initialized));
        return;
    }

    SetStatus(L"Initializing (ASUS can take about one minute)");
    CLSID classId{};
    HRESULT hr = CLSIDFromProgID(L"aura.sdk", &classId);
    ComPtr<IDispatch> sdk;
    if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(classId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sdk));
    }
    if (FAILED(hr) || !sdk) {
        SetStatus(L"Aura SDK is not installed");
        Logger::Instance().Info(L"Aura SDK unavailable; Dynamic Lighting remains enabled");
        if (SUCCEEDED(initialized)) {
            CoUninitialize();
        }
        return;
    }

    hr = InvokeNoArgs(sdk.Get(), L"SwitchMode");
    if (FAILED(hr)) {
        SetStatus(L"Could not acquire Aura control");
        Logger::Instance().Error(L"Aura SwitchMode failed: " + HResultMessage(hr));
        sdk.Reset();
        if (SUCCEEDED(initialized)) {
            CoUninitialize();
        }
        return;
    }

    VARIANT devicesResult;
    VariantInit(&devicesResult);
    hr = InvokeUnsigned(sdk.Get(), L"Enumerate", 0, &devicesResult);
    ComPtr<IDispatch> collection;
    if (SUCCEEDED(hr)) {
        collection = VariantDispatch(devicesResult);
    } else {
        VariantClear(&devicesResult);
    }
    if (FAILED(hr) || !collection) {
        SetStatus(L"Aura device enumeration failed");
        Logger::Instance().Error(L"Aura Enumerate failed: " + HResultMessage(hr));
        sdk.Reset();
        if (SUCCEEDED(initialized)) {
            CoUninitialize();
        }
        return;
    }

    std::vector<ComPtr<IDispatch>> devices;
    std::vector<std::vector<ComPtr<IDispatch>>> lights;
    const long count = GetLong(collection.Get(), L"Count");
    for (long index = 0; index < count; ++index) {
        auto device = CollectionItem(collection.Get(), index);
        if (!device) {
            continue;
        }
        auto lightCollection = GetDispatch(device.Get(), L"Lights");
        if (!lightCollection) {
            continue;
        }
        std::vector<ComPtr<IDispatch>> deviceLights;
        const long lightCount = GetLong(lightCollection.Get(), L"Count");
        for (long lightIndex = 0; lightIndex < lightCount; ++lightIndex) {
            auto light = CollectionItem(lightCollection.Get(), lightIndex);
            if (light) {
                deviceLights.push_back(std::move(light));
            }
        }
        if (!deviceLights.empty()) {
            Logger::Instance().Info(L"Aura device: " + GetString(device.Get(), L"Name") + L", lights " +
                                    std::to_wstring(deviceLights.size()));
            devices.push_back(std::move(device));
            lights.push_back(std::move(deviceLights));
        }
    }
    collection.Reset();

    deviceCount_.store(static_cast<int>(devices.size()));
    ready_.store(!devices.empty());
    SetStatus(devices.empty() ? L"No Aura devices" : L"Ready");
    Logger::Instance().Info(L"Aura initialization complete, devices: " +
                            std::to_wstring(devices.size()));

    std::uint64_t appliedGeneration = 0;
    while (!stop_.load()) {
        const std::uint64_t currentGeneration = generation_.load();
        if (!devices.empty() && currentGeneration != appliedGeneration) {
            const RgbColor color = RgbColor::FromPacked(latestColor_.load());
            // Aura SDK uses 0x00GGBBRR rather than the common 0x00RRGGBB order.
            const std::uint32_t auraColor = (static_cast<std::uint32_t>(color.g) << 16U) |
                                            (static_cast<std::uint32_t>(color.b) << 8U) |
                                            color.r;
            bool success = true;
            for (std::size_t deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex) {
                for (const auto& light : lights[deviceIndex]) {
                    if (FAILED(SetUnsignedProperty(light.Get(), L"Color", auraColor))) {
                        success = false;
                    }
                }
                if (FAILED(InvokeNoArgs(devices[deviceIndex].Get(), L"Apply"))) {
                    success = false;
                }
            }
            if (!success) {
                SetStatus(L"Aura update failed");
            }
            appliedGeneration = currentGeneration;
        }
        Sleep(10);
    }

    VARIANTARG releaseArgument;
    VariantInit(&releaseArgument);
    releaseArgument.vt = VT_UI4;
    releaseArgument.ulVal = 0;
    Invoke(sdk.Get(), L"ReleaseControl", DISPATCH_METHOD, &releaseArgument, 1, nullptr);
    ready_.store(false);
    lights.clear();
    devices.clear();
    sdk.Reset();
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
}

}  // namespace als
