
#ifndef __g_cclosure_user_marshal_MARSHAL_H__
#define __g_cclosure_user_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:BOXED,STRING (closures.def:1) */
extern void g_cclosure_user_marshal_VOID__BOXED_STRING (GClosure     *closure,
                                                        GValue       *return_value,
                                                        guint         n_param_values,
                                                        const GValue *param_values,
                                                        gpointer      invocation_hint,
                                                        gpointer      marshal_data);

/* VOID:BOXED,UINT,BOXED (closures.def:2) */
extern void g_cclosure_user_marshal_VOID__BOXED_UINT_BOXED (GClosure     *closure,
                                                            GValue       *return_value,
                                                            guint         n_param_values,
                                                            const GValue *param_values,
                                                            gpointer      invocation_hint,
                                                            gpointer      marshal_data);

/* VOID:BOXED,BOXED (closures.def:3) */
extern void g_cclosure_user_marshal_VOID__BOXED_BOXED (GClosure     *closure,
                                                       GValue       *return_value,
                                                       guint         n_param_values,
                                                       const GValue *param_values,
                                                       gpointer      invocation_hint,
                                                       gpointer      marshal_data);

/* VOID:BOOLEAN,INT,BOOLEAN,STRING (closures.def:4) */
extern void g_cclosure_user_marshal_VOID__BOOLEAN_INT_BOOLEAN_STRING (GClosure     *closure,
                                                                      GValue       *return_value,
                                                                      guint         n_param_values,
                                                                      const GValue *param_values,
                                                                      gpointer      invocation_hint,
                                                                      gpointer      marshal_data);

/* POINTER:STRING,OBJECT (closures.def:5) */
extern void g_cclosure_user_marshal_POINTER__STRING_OBJECT (GClosure     *closure,
                                                            GValue       *return_value,
                                                            guint         n_param_values,
                                                            const GValue *param_values,
                                                            gpointer      invocation_hint,
                                                            gpointer      marshal_data);

G_END_DECLS

#endif /* __g_cclosure_user_marshal_MARSHAL_H__ */

