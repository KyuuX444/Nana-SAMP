package com.wardrumstudios.utils;

/**
 * Clean from-scratch JNI host bridge implementing com/wardrumstudios/utils/WarBilling
 * method lookups performed by libGTASA.so (WarBillingInit).
 */
public abstract class WarBilling extends WarMedia {
    public native void notifyChange(String sku, int state);
    public native void changeConnection(boolean connected);

    public void AddSKU(String sku) {}
    public boolean InitBilling() { return false; }
    public boolean RequestPurchase(String sku) { return false; }
    public String LocalizedPrice(String sku) { return ""; }
    public void SetBillingKey(String key) {}
}
