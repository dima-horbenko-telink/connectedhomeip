/*
 *
 *    Copyright (c) 2023 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "AppTask.h"
#include "BoltLockManager.h"
#include <app/server/Server.h>
#include <credentials/FabricTable.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/data-model/Nullable.h>
LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip::app::Clusters::DoorLock;

namespace {
#if CONFIG_CHIP_ENABLE_APPLICATION_STATUS_LED
LEDWidget sLockLED;
#endif
} // namespace

#if defined(CONFIG_CHIP_LIB_SHELL)
#include "lib/shell/Engine.h"
#include "lib/shell/commands/Help.h"
#endif // CONFIG_CHIP_LIB_SHELL


#if defined(CONFIG_CHIP_LIB_SHELL)
using Shell::Engine;
using Shell::shell_command_t;
using Shell::streamer_get;
using Shell::streamer_printf;
using chip::app::DataModel::MakeNullable;
using ::chip::app::DataModel::Nullable;
using chip::app::DataModel::NullNullable;
Engine sShellDoorLockEvents;
Engine sShellDoorLockSubEvents;
#endif // defined(CONFIG_CHIP_LIB_SHELL)


AppTask AppTask::sAppTask;

CHIP_ERROR AppTask::Init(void)
{
#if APP_USE_EXAMPLE_START_BUTTON
    SetExampleButtonCallbacks(LockActionEventHandler);
#endif
    InitCommonParts();

#if CONFIG_CHIP_ENABLE_APPLICATION_STATUS_LED
    sLockLED.Init(GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios));
    sLockLED.Set(BoltLockMgr().IsLocked());
#endif

    BoltLockMgr().Init(LockStateChanged);

    // Disable auto-relock time feature.
    DoorLockServer::Instance().SetAutoRelockTime(kExampleEndpointId, 0);

    CHIP_ERROR err = ConnectivityMgr().SetBLEDeviceName("Telink Lock");
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("SetBLEDeviceName fail");
        return err;
    }

    InitDoorLockHandler();

    return CHIP_NO_ERROR;
}

void AppTask::LockActionEventHandler(AppEvent * aEvent)
{
    if (BoltLockMgr().IsLocked())
    {
        BoltLockMgr().Unlock(BoltLockManager::OperationSource::kButton);
    }
    else
    {
        BoltLockMgr().Lock(BoltLockManager::OperationSource::kButton);
    }
}

void AppTask::LockStateChanged(BoltLockManager::State state, BoltLockManager::OperationSource source)
{
    switch (state)
    {
    case BoltLockManager::State::kLockingInitiated:
        LOG_INF("Lock action initiated");
#if CONFIG_CHIP_ENABLE_APPLICATION_STATUS_LED
        sLockLED.Blink(50, 50);
#endif
        break;
    case BoltLockManager::State::kLockingCompleted:
        LOG_INF("Lock action completed");
#if CONFIG_CHIP_ENABLE_APPLICATION_STATUS_LED
        sLockLED.Set(true);
#endif
        break;
    case BoltLockManager::State::kUnlockingInitiated:
        LOG_INF("Unlock action initiated");
#if CONFIG_CHIP_ENABLE_APPLICATION_STATUS_LED
        sLockLED.Blink(50, 50);
#endif
        break;
    case BoltLockManager::State::kUnlockingCompleted:
        LOG_INF("Unlock action completed");
#if CONFIG_CHIP_ENABLE_APPLICATION_STATUS_LED
        sLockLED.Set(false);
#endif
        break;
    }

    // Handle changing attribute state in the application
    sAppTask.UpdateClusterState(state, source);
}

void AppTask::UpdateClusterState(BoltLockManager::State state, BoltLockManager::OperationSource source)
{
    DlLockState newLockState;

    switch (state)
    {
    case BoltLockManager::State::kLockingCompleted:
        newLockState = DlLockState::kLocked;
        break;
    case BoltLockManager::State::kUnlockingCompleted:
        newLockState = DlLockState::kUnlocked;
        break;
    default:
        newLockState = DlLockState::kNotFullyLocked;
        break;
    }

    SystemLayer().ScheduleLambda([newLockState, source] {
        chip::app::DataModel::Nullable<chip::app::Clusters::DoorLock::DlLockState> currentLockState;
        chip::app::Clusters::DoorLock::Attributes::LockState::Get(kExampleEndpointId, currentLockState);

        if (currentLockState.IsNull())
        {
            // Initialize lock state with start value, but not invoke lock/unlock.
            chip::app::Clusters::DoorLock::Attributes::LockState::Set(kExampleEndpointId, newLockState);
        }
        else
        {
            LOG_INF("Updating LockState attribute");

            if (!DoorLockServer::Instance().SetLockState(kExampleEndpointId, newLockState, source))
            {
                LOG_ERR("Failed to update LockState attribute");
            }
        }
    });
}



#ifdef CONFIG_CHIP_LIB_SHELL

/********************************************************
 * Switch shell functions
 *********************************************************/

CHIP_ERROR LockHelpHandler(int argc, char ** argv)
{
    sShellDoorLockEvents.ForEachCommand(Shell::PrintCommandHelp, nullptr);
    return CHIP_NO_ERROR;
}

CHIP_ERROR DoorLockEventsHandler(int argc, char ** argv)
{
    if (argc == 0)
    {
        return LockHelpHandler(argc, argv);
    }

    return sShellDoorLockEvents.ExecCommand(argc, argv);
}

/********************************************************
 * OnOff switch shell functions
 *********************************************************/

CHIP_ERROR DoorLockHelpHandler(int argc, char ** argv)
{
    sShellDoorLockSubEvents.ForEachCommand(Shell::PrintCommandHelp, nullptr);
    return CHIP_NO_ERROR;
}


CHIP_ERROR DoorLockLockCommandHandler(int argc, char ** argv)
{
    if (argc == 0)
    {
        return DoorLockHelpHandler(argc, argv);
    }

    return sShellDoorLockSubEvents.ExecCommand(argc, argv);
}


CHIP_ERROR AlarmReadEventHandler(int argc, char ** argv)
{
     DoorLockServer::Instance().SendLockAlarmEvent(1, AlarmCodeEnum::kLockJammed);
    return CHIP_NO_ERROR;
}

CHIP_ERROR StateChangeReadEventHandler(int argc, char ** argv)
{
     DoorLockServer::Instance().SetDoorState(1, DoorStateEnum::kDoorClosed);
    return CHIP_NO_ERROR;
}


/**
    //  * @brief Send LockOperation event if opSuccess is true, otherwise send LockOperationError with given opErr code
    //  *
    //  * @param endpointId    endpoint where DoorLockServer is running
    //  * @param opType        lock operation type (lock, unlock, etc)
    //  * @param opSource      operation source (remote, keypad, auto, etc)
    //  * @param opErr         operation error code (if opSuccess == false)
    //  * @param userId        user id
    //  * @param fabricIdx     fabric index responsible for operating the lock
    //  * @param nodeId        node id responsible for operating the lock
    //  * @param credList      list of credentials used in lock operation (can be NULL if no credentials were used)
    //  * @param credListSize  size of credentials list (if 0, then no credentials were used)
    //  * @param opSuccess     flags if operation was successful or not
    //  */
    // void SendLockOperationEvent(chip::EndpointId endpointId, LockOperationTypeEnum opType, OperationSourceEnum opSource,
    //                             OperationErrorEnum opErr, const Nullable<uint16_t> & userId,
    //                             const Nullable<chip::FabricIndex> & fabricIdx, const Nullable<chip::NodeId> & nodeId,
    //                             const Nullable<List<const LockOpCredentials>> & credentials = NullNullable, bool opSuccess = true);


CHIP_ERROR OperationReadEventHandler(int argc, char ** argv)
{
    // ChipLogError(Zcl, "$$$$$$$$$$OperationReadEventHandler");
    // LockOpCredentials currentCredential;
    // if (emberAfPluginDoorLockGetCredential(1, 2, CredentialTypeEnum::kRfid, currentCredential))
    // {
    //     ChipLogError(Zcl, "$$$$$$$$$$OperationReadEventHandler OK");
    // }

    // NodeId nodeid= 1;


    // for (const auto & fabricInfo : : chip::Server::GetInstance().GetFabricTable())
    // {
    //     FabricIndex fabricIndex =  MakeNullable(fabricInfo.GetFabricIndex());

    // DoorLockServer::Instance().SendLockOperationEvent(1, LockOperationTypeEnum::kLock, OperationSourceEnum::kRemote, OperationErrorEnum::kUnspecified, chip::app::DataModel::MakeNullable(static_cast<uint16_t>(1)), lift, lift1,
    //                        currentCredential, true);
    // }

    // for (const auto & fb : chip::Server::GetInstance().GetFabricTable())
    // {
    //     FabricIndex fabricIndex = fb.GetFabricIndex();
    //     NodeId myNodeId = fb.GetNodeId();
        // ChipLogProgress(NotSpecified,
        //                 "---- Current Fabric nodeId=0x" ChipLogFormatX64 " fabricId=0x" ChipLogFormatX64 " fabricIndex=%d",
        //                 ChipLogValueX64(myNodeId), ChipLogValueX64(fb.GetFabricId()), fabricIndex);
        // DoorLockServer::Instance().SendLockOperationEvent(1, LockOperationTypeEnum::kLock, OperationSourceEnum::kRemote, OperationErrorEnum::kUnspecified, NullNullable, NullNullable, NullNullable,
        //                    NullNullable);
        DoorLockServer::Instance().SetLockState(1, DlLockState::kLocked);
    // }

    return CHIP_NO_ERROR;
}


// CHIP_ERROR OnOffSwitchCommandHandler(int argc, char ** argv)
// {
//     if (argc == 0)
//     {
//         return OnOffHelpHandler(argc, argv);
//     }

//     return sShellSwitchOnOffSubCommands.ExecCommand(argc, argv);
// }

// CHIP_ERROR OnSwitchCommandHandler(int argc, char ** argv)
// {
//     BindingCommandData * data = Platform::New<BindingCommandData>();
//     data->commandId           = Clusters::OnOff::Commands::On::Id;
//     data->clusterId           = Clusters::OnOff::Id;

//     DeviceLayer::PlatformMgr().ScheduleWork(SwitchWorkerFunction, reinterpret_cast<intptr_t>(data));
//     return CHIP_NO_ERROR;
// }

// CHIP_ERROR OffSwitchCommandHandler(int argc, char ** argv)
// {
//     BindingCommandData * data = Platform::New<BindingCommandData>();
//     data->commandId           = Clusters::OnOff::Commands::Off::Id;
//     data->clusterId           = Clusters::OnOff::Id;

//     DeviceLayer::PlatformMgr().ScheduleWork(SwitchWorkerFunction, reinterpret_cast<intptr_t>(data));
//     return CHIP_NO_ERROR;
// }

// CHIP_ERROR ToggleSwitchCommandHandler(int argc, char ** argv)
// {
//     BindingCommandData * data = Platform::New<BindingCommandData>();
//     data->commandId           = Clusters::OnOff::Commands::Toggle::Id;
//     data->clusterId           = Clusters::OnOff::Id;

//     DeviceLayer::PlatformMgr().ScheduleWork(SwitchWorkerFunction, reinterpret_cast<intptr_t>(data));
//     return CHIP_NO_ERROR;
// }

// /********************************************************
//  * bind switch shell functions
//  *********************************************************/

// CHIP_ERROR BindingHelpHandler(int argc, char ** argv)
// {
//     sShellSwitchBindingSubCommands.ForEachCommand(Shell::PrintCommandHelp, nullptr);
//     return CHIP_NO_ERROR;
// }

// CHIP_ERROR BindingSwitchCommandHandler(int argc, char ** argv)
// {
//     if (argc == 0)
//     {
//         return BindingHelpHandler(argc, argv);
//     }

//     return sShellSwitchBindingSubCommands.ExecCommand(argc, argv);
// }

// CHIP_ERROR BindingGroupBindCommandHandler(int argc, char ** argv)
// {
//     VerifyOrReturnError(argc == 2, CHIP_ERROR_INVALID_ARGUMENT);

//     EmberBindingTableEntry * entry = Platform::New<EmberBindingTableEntry>();
//     entry->type                    = EMBER_MULTICAST_BINDING;
//     entry->fabricIndex             = atoi(argv[0]);
//     entry->groupId                 = atoi(argv[1]);
//     entry->local                   = 1; // Hardcoded to endpoint 1 for now
//     entry->clusterId.SetValue(6);       // Hardcoded to OnOff cluster for now

//     DeviceLayer::PlatformMgr().ScheduleWork(BindingWorkerFunction, reinterpret_cast<intptr_t>(entry));
//     return CHIP_NO_ERROR;
// }

// CHIP_ERROR BindingUnicastBindCommandHandler(int argc, char ** argv)
// {
//     VerifyOrReturnError(argc == 3, CHIP_ERROR_INVALID_ARGUMENT);

//     EmberBindingTableEntry * entry = Platform::New<EmberBindingTableEntry>();
//     entry->type                    = EMBER_UNICAST_BINDING;
//     entry->fabricIndex             = atoi(argv[0]);
//     entry->nodeId                  = atoi(argv[1]);
//     entry->local                   = 1; // Hardcoded to endpoint 1 for now
//     entry->remote                  = atoi(argv[2]);
//     entry->clusterId.SetValue(6); // Hardcode to OnOff cluster for now

//     DeviceLayer::PlatformMgr().ScheduleWork(BindingWorkerFunction, reinterpret_cast<intptr_t>(entry));
//     return CHIP_NO_ERROR;
// }

// /********************************************************
//  * Groups switch shell functions
//  *********************************************************/

// CHIP_ERROR GroupsHelpHandler(int argc, char ** argv)
// {
//     sShellSwitchGroupsSubCommands.ForEachCommand(Shell::PrintCommandHelp, nullptr);
//     return CHIP_NO_ERROR;
// }

// CHIP_ERROR GroupsSwitchCommandHandler(int argc, char ** argv)
// {
//     if (argc == 0)
//     {
//         return GroupsHelpHandler(argc, argv);
//     }

//     return sShellSwitchGroupsSubCommands.ExecCommand(argc, argv);
// }

// /********************************************************
//  * Groups OnOff switch shell functions
//  *********************************************************/

// CHIP_ERROR GroupsOnOffHelpHandler(int argc, char ** argv)
// {
//     sShellSwitchGroupsOnOffSubCommands.ForEachCommand(Shell::PrintCommandHelp, nullptr);
//     return CHIP_NO_ERROR;
// }

// CHIP_ERROR GroupsOnOffSwitchCommandHandler(int argc, char ** argv)
// {
//     if (argc == 0)
//     {
//         return GroupsOnOffHelpHandler(argc, argv);
//     }

//     return sShellSwitchGroupsOnOffSubCommands.ExecCommand(argc, argv);
// }

// CHIP_ERROR GroupOnSwitchCommandHandler(int argc, char ** argv)
// {
//     BindingCommandData * data = Platform::New<BindingCommandData>();
//     data->commandId           = Clusters::OnOff::Commands::On::Id;
//     data->clusterId           = Clusters::OnOff::Id;
//     data->isGroup             = true;

//     DeviceLayer::PlatformMgr().ScheduleWork(SwitchWorkerFunction, reinterpret_cast<intptr_t>(data));
//     return CHIP_NO_ERROR;
// }

// CHIP_ERROR GroupOffSwitchCommandHandler(int argc, char ** argv)
// {
//     BindingCommandData * data = Platform::New<BindingCommandData>();
//     data->commandId           = Clusters::OnOff::Commands::Off::Id;
//     data->clusterId           = Clusters::OnOff::Id;
//     data->isGroup             = true;

//     DeviceLayer::PlatformMgr().ScheduleWork(SwitchWorkerFunction, reinterpret_cast<intptr_t>(data));
//     return CHIP_NO_ERROR;
// }

// CHIP_ERROR GroupToggleSwitchCommandHandler(int argc, char ** argv)
// {
//     BindingCommandData * data = Platform::New<BindingCommandData>();
//     data->commandId           = Clusters::OnOff::Commands::Toggle::Id;
//     data->clusterId           = Clusters::OnOff::Id;
//     data->isGroup             = true;

//     DeviceLayer::PlatformMgr().ScheduleWork(SwitchWorkerFunction, reinterpret_cast<intptr_t>(data));
//     return CHIP_NO_ERROR;
// }

/**
 * @brief configures switch matter shell
 *
 */
static void RegisterDoorLockCommands()
{
    static const shell_command_t sDoorLockSubCommands[] = {
        { &LockHelpHandler, "help", "Usage: lock <subcommand>" },
        { &DoorLockLockCommandHandler, "doorlock", " Usage: lock doorlock <subcommand>" },
    };

    static const shell_command_t sLockDoorLockSubCommands[] = {
        { &DoorLockHelpHandler,"help", "Usage : lock doorlock <subcommand>" },
        { &AlarmReadEventHandler,"DoorLockAlarm", " lock doorlock DoorLockAlarm"},
        { &StateChangeReadEventHandler,"DoorStateChange", "lock doorlock DoorStateChange"},
        { &OperationReadEventHandler,"LockOperation", "lock doorlock LockOperation" }
        // { &OperationErrorReadEventHandler, "LockOperationError", "lock doorlock LockOperationError" },
        // { &UserChangeReadEventHandler,"LockUserChange", "lock doorlock LockUserChange" }
    };

    // static const shell_command_t sSwitchGroupsSubCommands[] = { { &GroupsHelpHandler, "help", "Usage: switch groups <subcommand>" },
    //                                                             { &GroupsOnOffSwitchCommandHandler, "onoff",
    //                                                               "Usage: switch groups onoff <subcommand>" } };

    // static const shell_command_t sSwitchGroupsOnOffSubCommands[] = {
    //     { &GroupsOnOffHelpHandler, "help", "Usage: switch groups onoff <subcommand>" },
    //     { &GroupOnSwitchCommandHandler, "on", "Sends on command to bound group" },
    //     { &GroupOffSwitchCommandHandler, "off", "Sends off command to bound group" },
    //     { &GroupToggleSwitchCommandHandler, "toggle", "Sends toggle command to group" }
    // };

    // static const shell_command_t sSwitchBindingSubCommands[] = {
    //     { &BindingHelpHandler, "help", "Usage: switch binding <subcommand>" },
    //     { &BindingGroupBindCommandHandler, "group", "Usage: switch binding group <fabric index> <group id>" },
    //     { &BindingUnicastBindCommandHandler, "unicast", "Usage: switch binding group <fabric index> <node id> <endpoint>" }
    // };

    static const shell_command_t sLockCommand = { &DoorLockEventsHandler, "lock",
                                                    "Door-Lock commands. Usage: lock <subcommand>" };

    // sShellSwitchGroupsOnOffSubCommands.RegisterCommands(sSwitchGroupsOnOffSubCommands, ArraySize(sSwitchGroupsOnOffSubCommands));
    // sShellSwitchOnOffSubCommands.RegisterCommands(sSwitchOnOffSubCommands, ArraySize(sSwitchOnOffSubCommands));
    // sShellSwitchGroupsSubCommands.RegisterCommands(sSwitchGroupsSubCommands, ArraySize(sSwitchGroupsSubCommands));
    sShellDoorLockSubEvents.RegisterCommands(sLockDoorLockSubCommands, ArraySize(sLockDoorLockSubCommands));
    sShellDoorLockEvents.RegisterCommands(sDoorLockSubCommands, ArraySize(sDoorLockSubCommands));

    Engine::Root().RegisterCommands(&sLockCommand, 1);
}
#endif // CONFIG_CHIP_LIB_SHELL








CHIP_ERROR InitDoorLockHandler()
{
    // The initialization of binding manager will try establishing connection with unicast peers
    // so it requires the Server instance to be correctly initialized. Post the init function to
    // the event queue so that everything is ready when initialization is conducted.
#if defined(CONFIG_CHIP_LIB_SHELL)
    RegisterDoorLockCommands();
#endif
    return CHIP_NO_ERROR;
}