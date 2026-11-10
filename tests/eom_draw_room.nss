// Local EOM drawing proof only. Jump to the authored dormitory arrival waypoint.
void main()
{
    object oPC = OBJECT_SELF;
    if (!GetIsPC(oPC)) oPC = GetLastUsedBy();
    if (!GetIsPC(oPC)) return;
    object oArrival = GetObjectByTag("wm_dor_arr");
    if (!GetIsObjectValid(oArrival))
    {
        SendMessageToPC(oPC, "The dormitory arrival waypoint wm_dor_arr is unavailable.");
        return;
    }
    AssignCommand(oPC, JumpToLocation(GetLocation(oArrival)));
    DelayCommand(1.0, ExecuteScript("eom_draw_probe", oPC));
}
